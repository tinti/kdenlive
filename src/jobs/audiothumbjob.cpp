/***************************************************************************
 *   Copyright (C) 2017 by Nicolas Carion                                  *
 *   This file is part of Kdenlive. See www.kdenlive.org.                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) version 3 or any later version accepted by the       *
 *   membership of KDE e.V. (or its successor approved  by the membership  *
 *   of KDE e.V.), which shall act as a proxy defined in Section 14 of     *
 *   version 3 of the license.                                             *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#include "audiothumbjob.hpp"
#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "doc/kdenlivedoc.h"
#include "doc/kthumb.h"
#include "kdenlivesettings.h"
#include "klocalizedstring.h"
#include "lib/audio/audioStreamInfo.h"
#include "macros.hpp"
#include "utils/thumbnailcache.hpp"
#include <QScopedPointer>
#include <QTemporaryFile>
#include <QProcess>
#include <memory>
#include <mlt++/MltProducer.h>

AudioThumbJob::AudioThumbJob(const QString &binId)
    : AbstractClipJob(AUDIOTHUMBJOB, binId)
    , m_ffmpegProcess(nullptr)
{
}

const QString AudioThumbJob::getDescription() const
{
    return i18n("Extracting audio thumb from clip %1", m_clipId);
}

bool AudioThumbJob::computeWithMlt()
{
    m_audioLevels.clear();
    m_errorMessage.clear();
    // MLT audio thumbs: slower but safer
    QString service = m_prod->get("mlt_service");
    if (service == QLatin1String("avformat-novalidate")) {
        service = QStringLiteral("avformat");
    } else if (service.startsWith(QLatin1String("xml"))) {
        service = QStringLiteral("xml-nogl");
    }
    QScopedPointer<Mlt::Producer> audioProducer(new Mlt::Producer(*m_prod->profile(), service.toUtf8().constData(), m_prod->get("resource")));
    if (!audioProducer->is_valid()) {
        m_errorMessage.append(i18n("Audio thumbs: cannot open file %1", m_prod->get("resource")));
        return false;
    }
    audioProducer->set("video_index", "-1");
    Mlt::Filter chans(*m_prod->profile(), "audiochannels");
    Mlt::Filter converter(*m_prod->profile(), "audioconvert");
    Mlt::Filter levels(*m_prod->profile(), "audiolevel");
    audioProducer->attach(chans);
    audioProducer->attach(converter);
    audioProducer->attach(levels);

    int last_val = 0;
    double framesPerSecond = audioProducer->get_fps();
    mlt_audio_format audioFormat = mlt_audio_s16;
    QStringList keys;
    keys.reserve(m_channels);
    for (int i = 0; i < m_channels; i++) {
        keys << "meta.media.audio_level." + QString::number(i);
    }

    for (int z = 0; z < m_lengthInFrames; ++z) {
        int val = (int)(100.0 * z / m_lengthInFrames);
        if (last_val != val) {
            emit jobProgress(val);
            last_val = val;
        }
        QScopedPointer<Mlt::Frame> mltFrame(audioProducer->get_frame());
        if ((mltFrame != nullptr) && mltFrame->is_valid() && (mltFrame->get_int("test_audio") == 0)) {
            int samples = mlt_sample_calculator(float(framesPerSecond), m_frequency, z);
            mltFrame->get_audio(audioFormat, m_frequency, m_channels, samples);
            for (int channel = 0; channel < m_channels; ++channel) {
                double level = 256 * qMin(mltFrame->get_double(keys.at(channel).toUtf8().constData()) * 0.9, 1.0);
                m_audioLevels << level;
            }
        } else if (!m_audioLevels.isEmpty()) {
            for (int channel = 0; channel < m_channels; channel++) {
                m_audioLevels << m_audioLevels.last();
            }
        }
    }
    m_done = true;
    return true;
}

bool AudioThumbJob::computeWithFFMPEG()
{
    QString filePath = m_prod->get("kdenlive:originalurl");
    if (filePath.isEmpty()) {
        filePath = m_prod->get("resource");
    }
    m_ffmpegProcess.reset(new QProcess);
    if (!m_thumbInCache) {
        QStringList args;
        args << QStringLiteral("-hide_banner") << QStringLiteral("-y")<< QStringLiteral("-i") << QUrl::fromLocalFile(filePath).toLocalFile() << QStringLiteral("-filter_complex:a");
        args << QString("showwavespic=s=%1x%2:split_channels=1:scale=cbrt:colors=0xffdddd|0xddffdd").arg(m_thumbSize.width()).arg(m_thumbSize.height());
        args << QStringLiteral("-frames:v") << QStringLiteral("1");
        args << m_binClip->getAudioThumbPath(true);
        connect(m_ffmpegProcess.get(), &QProcess::readyReadStandardOutput, this, &AudioThumbJob::updateFfmpegProgress, Qt::UniqueConnection);
        connect(this, &AudioThumbJob::jobCanceled, [&] () {
            if (m_ffmpegProcess) {
                m_ffmpegProcess->kill();
            }
            m_done = true;
            m_successful = false;
        });
        m_ffmpegProcess->start(KdenliveSettings::ffmpegpath(), args);
        m_ffmpegProcess->waitForFinished(-1);
        if (m_ffmpegProcess->exitStatus() != QProcess::CrashExit) {
            m_thumbInCache = true;
            if (m_dataInCache) {
                m_done = true;
                return true;
            } else {
                // Next Processing step can be long, already display audio thumb in monitor
                m_binClip->audioThumbReady();
            }
        }
    }
    if (!m_dataInCache && !m_done) {
        m_audioLevels.clear();
        std::vector<std::unique_ptr<QTemporaryFile>> channelFiles;
        for (int i = 0; i < m_channels; i++) {
            std::unique_ptr<QTemporaryFile> channelTmpfile(new QTemporaryFile());
            if (!channelTmpfile->open()) {
                m_errorMessage.append(i18n("Audio thumbs: cannot create temporary file, check disk space and permissions\n"));
                return false;
            }
            channelTmpfile->close();
            channelFiles.emplace_back(std::move(channelTmpfile));
        }
        // Always create audio thumbs from the original source file, because proxy 
        // can have a different audio config (channels / mono/ stereo)
        QStringList args {QStringLiteral("-hide_banner"), QStringLiteral("-i"), QUrl::fromLocalFile(filePath).toLocalFile(), QStringLiteral("-progress")};
#ifdef Q_OS_WIN
        args << QStringLiteral("-");
#else
        args << QStringLiteral("/dev/stdout");
#endif
        bool isFFmpeg = KdenliveSettings::ffmpegpath().contains(QLatin1String("ffmpeg"));
        args << QStringLiteral("-filter_complex:a");
        if (m_channels == 1) {
            //TODO: this does not correcty generate the correct stream data
            args << QStringLiteral("aformat=channel_layouts=mono,%1=100").arg(isFFmpeg ? "aresample=async" : "sample_rates");
            args << QStringLiteral("-map") << QStringLiteral("0:a%1").arg(m_audioStream > 0 ? ":" + QString::number(m_audioStream) : QString())
             << QStringLiteral("-c:a") << QStringLiteral("pcm_s16le") << QStringLiteral("-frames:v") 
             << QStringLiteral("1") << QStringLiteral("-y") << QStringLiteral("-f") << QStringLiteral("data")
             << channelFiles[0]->fileName();
        } else {
            QString aformat = QStringLiteral("[0:a%1]%2=100,channelsplit=channel_layout=%3")
                              .arg(m_audioStream > 0 ? ":" + QString::number(m_audioStream) : QString())
                              .arg(isFFmpeg ? "aresample=async" : "aformat=sample_rates=")
                              .arg(m_channels > 2 ? "5.1" : "stereo");
            for (int i = 0; i < m_channels; ++i) {
                aformat.append(QStringLiteral("[0:%1]").arg(i));
            }
            args << aformat;
            args << QStringLiteral("-frames:v") << QStringLiteral("1");
            for (int i = 0; i < m_channels; i++) {
                // Channel 1
                args << QStringLiteral("-map") << QStringLiteral("[0:%1]").arg(i) << QStringLiteral("-c:a") << QStringLiteral("pcm_s16le") << QStringLiteral("-y")
                 << QStringLiteral("-f") << QStringLiteral("data") << channelFiles[size_t(i)]->fileName();
            }
        }
        m_ffmpegProcess.reset(new QProcess);
        connect(m_ffmpegProcess.get(), &QProcess::readyReadStandardOutput, this, &AudioThumbJob::updateFfmpegProgress, Qt::UniqueConnection);
        connect(this, &AudioThumbJob::jobCanceled, m_ffmpegProcess.get(), &QProcess::kill, Qt::DirectConnection);
        m_ffmpegProcess->start(KdenliveSettings::ffmpegpath(), args);
        m_ffmpegProcess->waitForFinished(-1);
        if (m_ffmpegProcess->exitStatus() != QProcess::CrashExit) {
            int dataSize = 0;
            std::vector<const qint16 *> rawChannels;
            std::vector<QByteArray> sourceChannels;
            for (auto &channelFile : channelFiles) {
                channelFile->open();
                sourceChannels.emplace_back(channelFile->readAll());
                QByteArray &res = sourceChannels.back();
                channelFile->close();
                if (dataSize == 0) {
                    dataSize = res.size();
                }
                if (res.isEmpty() || res.size() != dataSize) {
                    // Something went wrong, abort
                    m_errorMessage.append(i18n("Audio thumbs: error reading audio thumbnail created with FFmpeg\n"));
                    return false;
                }
                rawChannels.emplace_back((const qint16 *)res.constData());
            }
            int progress = 0;
            std::vector<long> channelsData;
            double offset = (double)dataSize / (2.0 * m_lengthInFrames);
            int intraOffset = 1;
            if (offset > 1000) {
                intraOffset = offset / 60;
            } else if (offset > 250) {
                intraOffset = offset / 10;
            }
            double factor = 800.0 / 32768;
            for (int i = 0; i < m_lengthInFrames; i++) {
                channelsData.resize((size_t)rawChannels.size());
                std::fill(channelsData.begin(), channelsData.end(), 0);
                int pos = (int)(i * offset);
                int steps = 0;
                for (int j = 0; j < (int)offset && (pos + j < dataSize); j += intraOffset) {
                    steps++;
                    for (size_t k = 0; k < rawChannels.size(); k++) {
                    channelsData[k] += abs(rawChannels[k][pos + j]);
                    }
                }
                for (long &k : channelsData) {
                    if (steps != 0) {
                        k /= steps;
                    }
                    m_audioLevels << (int)((double)k * factor);
                }
                int p = 80 + (i * 20 / m_lengthInFrames);
                if (p != progress) {
                    emit jobProgress(p);
                    progress = p;
                }
            }
            m_done = true;
            return true;
        }
    }
    QString err = m_ffmpegProcess->readAllStandardError();
    m_ffmpegProcess.reset();
    // m_errorMessage += err;
    // m_errorMessage.append(i18n("Failed to create FFmpeg audio thumbnails, we now try to use MLT"));
    qWarning() << "Failed to create FFmpeg audio thumbs:\n" << err << "\n---------------------";
    return false;
}

void AudioThumbJob::updateFfmpegProgress()
{
    QString result = m_ffmpegProcess->readAllStandardOutput();
    const QStringList lines = result.split(QLatin1Char('\n'));
    for (const QString &data : lines) {
        if (data.startsWith(QStringLiteral("out_time_ms"))) {
            double ms = data.section(QLatin1Char('='), 1).toDouble();
            emit jobProgress((int)(ms / m_binClip->duration().ms() / 10));
        } else {
            m_logDetails += data + QStringLiteral("\n");
        }
    }
}

bool AudioThumbJob::startJob()
{
    if (m_done) {
        return true;
    }
    m_dataInCache = false;
    m_thumbInCache = false;
    m_binClip = pCore->projectItemModel()->getClipByBinID(m_clipId);
    if (m_binClip->audioChannels() == 0 || m_binClip->audioThumbCreated()) {
        // nothing to do
        m_done = true;
        m_successful = true;
        return true;
    }
    m_thumbSize = QSize(1000, 1000 / pCore->getCurrentDar());
    m_prod = m_binClip->originalProducer();

    m_frequency = m_binClip->audioInfo()->samplingRate();
    m_frequency = m_frequency <= 0 ? 48000 : m_frequency;

    m_channels = m_binClip->audioInfo()->channels();
    m_channels = m_channels <= 0 ? 2 : m_channels;

    m_lengthInFrames = m_prod->get_length();
    m_audioStream = m_binClip->audioInfo()->ffmpeg_audio_index();
    if ((m_prod == nullptr) || !m_prod->is_valid()) {
        m_errorMessage.append(i18n("Audio thumbs: cannot open project file %1", m_binClip->url()));
        m_done = true;
        m_successful = false;
        return false;
    }
    m_cachePath = m_binClip->getAudioThumbPath();

    // checking for cached thumbs
    QImage image(m_cachePath);
    if (!image.isNull()) {
        // convert cached image
        int n = image.width() * image.height();
        for (int i = 0; i < n; i++) {
            QRgb p = image.pixel(i / m_channels, i % m_channels);
            m_audioLevels << qRed(p);
            m_audioLevels << qGreen(p);
            m_audioLevels << qBlue(p);
            m_audioLevels << qAlpha(p);
        }
    }
    if (!m_audioLevels.isEmpty()) {
        m_dataInCache = true;
    }
    
    // Check audio thumbnail image
    if (ThumbnailCache::get()->hasThumbnail(m_clipId, -1, false)) {
        m_thumbInCache = true;
    }
    if (m_thumbInCache && m_dataInCache) {
        m_done = true;
        m_successful = true;
        return true;
    }
    
    bool ok = m_binClip->clipType() == ClipType::Playlist ? false : computeWithFFMPEG();
    ok = ok ? ok : computeWithMlt();
    Q_ASSERT(ok == m_done);

    if (ok && m_done && !m_dataInCache && !m_audioLevels.isEmpty()) {
        // Put into an image for caching.
        int count = m_audioLevels.size();
        image = QImage((int)lrint((count + 3) / 4.0 / m_channels), m_channels, QImage::Format_ARGB32);
        int n = image.width() * image.height();
        for (int i = 0; i < n; i++) {
            QRgb p;
            if ((4 * i + 3) < count) {
                p = qRgba(m_audioLevels.at(4 * i), m_audioLevels.at(4 * i + 1), m_audioLevels.at(4 * i + 2),
                          m_audioLevels.at(4 * i + 3));
            } else {
                int last = m_audioLevels.last();
                int r = (4 * i + 0) < count ? m_audioLevels.at(4 * i + 0) : last;
                int g = (4 * i + 1) < count ? m_audioLevels.at(4 * i + 1) : last;
                int b = (4 * i + 2) < count ? m_audioLevels.at(4 * i + 2) : last;
                int a = last;
                p = qRgba(r, g, b, a);
            }
            image.setPixel(i / m_channels, i % m_channels, p);
        }
        image.save(m_cachePath);
        m_successful = true;
        return true;
    } else if (ok && m_thumbInCache && m_done) {
        m_successful = true;
        return true;
    }
    m_done = true;
    m_successful = false;
    return false;
}

bool AudioThumbJob::commitResult(Fun &undo, Fun &redo)
{
    Q_ASSERT(!m_resultConsumed);
    m_ffmpegProcess.reset();
    if (!m_done) {
        qDebug() << "ERROR: Trying to consume invalid results";
        return false;
    }
    m_resultConsumed = true;
    if (!m_successful) {
        return false;
    }
    QList <double>old = m_binClip->audioFrameCache;
    QImage oldImage = m_binClip->thumbnail(m_thumbSize.width(), m_thumbSize.height()).toImage();
    QImage result = ThumbnailCache::get()->getAudioThumbnail(m_clipId);

    // note that the image is moved into lambda, it won't be available from this class anymore
    auto operation = [clip = m_binClip, audio = std::move(m_audioLevels), image = std::move(result)]() {
        clip->updateAudioThumbnail(audio);
        if (!image.isNull() && clip->clipType() == ClipType::Audio) {
            clip->setThumbnail(image);
        }
        return true;
    };
    auto reverse = [clip = m_binClip, audio = std::move(old), image = std::move(oldImage)]() {
        clip->updateAudioThumbnail(audio);
        if (!image.isNull() && clip->clipType() == ClipType::Audio) {
            clip->setThumbnail(image);
        }
        return true;
    };
    bool ok = operation();
    if (ok) {
        UPDATE_UNDO_REDO_NOLOCK(operation, reverse, undo, redo);
    }
    return ok;
}
