/*
Copyright (C) 2015  Jean-Baptiste Mardelle <jb@kdenlive.org>
This file is part of Kdenlive. See www.kdenlive.org.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of
the License or (at your option) version 3 or any later version
accepted by the membership of KDE e.V. (or its successor approved
by the membership of KDE e.V.), which shall act as a proxy
defined in Section 14 of version 3 of the license.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "projectsubclip.h"
#include "projectclip.h"
#include "projectitemmodel.h"
#include "core.h"
#include "doc/kdenlivedoc.h"
#include "doc/docundostack.hpp"
#include "bincommands.h"
#include "jobs/jobmanager.h"
#include "jobs/cachejob.hpp"
#include "utils/thumbnailcache.hpp"

#include <KLocalizedString>
#include <QDomElement>
#include <QPainter>
#include <utility>

class ClipController;

ProjectSubClip::ProjectSubClip(const QString &id, const std::shared_ptr<ProjectClip> &parent, const std::shared_ptr<ProjectItemModel> &model, int in, int out,
                               const QString &timecode, const QString &name)
    : AbstractProjectItem(AbstractProjectItem::SubClipItem, id, model)
    , m_masterClip(parent)
{
    m_inPoint = in;
    m_outPoint = out;
    m_duration = timecode;
    m_parentDuration = (int)m_masterClip->frameDuration();
    m_parentClipId = m_masterClip->clipId();
    QPixmap pix(64, 36);
    pix.fill(Qt::lightGray);
    m_thumbnail = QIcon(pix);
    if (name.isEmpty()) {
        m_name = i18n("Zone %1", parent->childCount() + 1);
    } else {
        m_name = name;
    }
    m_clipStatus = StatusReady;
    // Save subclip in MLT
    connect(parent.get(), &ProjectClip::thumbReady, this, &ProjectSubClip::gotThumb);
}

std::shared_ptr<ProjectSubClip> ProjectSubClip::construct(const QString &id, const std::shared_ptr<ProjectClip> &parent,
                                                          const std::shared_ptr<ProjectItemModel> &model, int in, int out, const QString &timecode,
                                                          const QString &name)
{
    std::shared_ptr<ProjectSubClip> self(new ProjectSubClip(id, parent, model, in, out, timecode, name));
    baseFinishConstruct(self);
    return self;
}

ProjectSubClip::~ProjectSubClip()
{
    // controller is deleted in bincontroller
}

void ProjectSubClip::gotThumb(int pos, const QImage &img)
{
    if (pos == m_inPoint) {
        setThumbnail(img);
        disconnect(m_masterClip.get(), &ProjectClip::thumbReady, this, &ProjectSubClip::gotThumb);
    }
}

QString ProjectSubClip::getToolTip() const
{
    return QString("%1-%2").arg(m_inPoint).arg(m_outPoint);
}

std::shared_ptr<ProjectClip> ProjectSubClip::clip(const QString &id)
{
    Q_UNUSED(id);
    return std::shared_ptr<ProjectClip>();
}

std::shared_ptr<ProjectFolder> ProjectSubClip::folder(const QString &id)
{
    Q_UNUSED(id);
    return std::shared_ptr<ProjectFolder>();
}

void ProjectSubClip::setBinEffectsEnabled(bool) {}

GenTime ProjectSubClip::duration() const
{
    // TODO
    return {};
}

QPoint ProjectSubClip::zone() const
{
    return {m_inPoint, m_outPoint};
}

std::shared_ptr<ProjectClip> ProjectSubClip::clipAt(int ix)
{
    Q_UNUSED(ix);
    return std::shared_ptr<ProjectClip>();
}

QDomElement ProjectSubClip::toXml(QDomDocument &document, bool, bool)
{
    QDomElement sub = document.createElement(QStringLiteral("subclip"));
    sub.setAttribute(QStringLiteral("id"), m_masterClip->AbstractProjectItem::clipId());
    sub.setAttribute(QStringLiteral("in"), m_inPoint);
    sub.setAttribute(QStringLiteral("out"), m_outPoint);
    return sub;
}

std::shared_ptr<ProjectSubClip> ProjectSubClip::subClip(int in, int out)
{
    if (m_inPoint == in && m_outPoint == out) {
        return std::static_pointer_cast<ProjectSubClip>(shared_from_this());
    }
    return std::shared_ptr<ProjectSubClip>();
}

void ProjectSubClip::setThumbnail(const QImage &img)
{
    QPixmap thumb = roundedPixmap(QPixmap::fromImage(img));
    int duration = m_parentDuration;
    double factor = ((double) thumb.width()) / duration;
    int zoneOut = m_outPoint - duration;
    QRect zoneRect(0, 0, thumb.width(), thumb.height());
    zoneRect.adjust(0, zoneRect.height() * 0.9, 0, -zoneRect.height() * 0.05);
    QPainter painter(&thumb);
    painter.fillRect(zoneRect, Qt::darkGreen);
    zoneRect.adjust(m_inPoint * factor, 0, zoneOut * factor, 0);
    painter.fillRect(zoneRect, Qt::green);
    painter.end();
    m_thumbnail = QIcon(thumb);
    if (auto ptr = m_model.lock())
        std::static_pointer_cast<ProjectItemModel>(ptr)->onItemUpdated(std::static_pointer_cast<ProjectSubClip>(shared_from_this()),
                                                                       AbstractProjectItem::DataThumbnail);
}

QPixmap ProjectSubClip::thumbnail(int width, int height)
{
    return m_thumbnail.pixmap(width, height);
}

bool ProjectSubClip::rename(const QString &name, int column)
{
    // TODO refac: rework this
    Q_UNUSED(column)
    if (m_name == name) {
        return false;
    }
    // Rename folder
    auto *command = new RenameBinSubClipCommand(pCore->bin(), m_masterClip->clipId(), name, m_name, m_inPoint, m_outPoint);
    pCore->currentDoc()->commandStack()->push(command);
    return true;
}

std::shared_ptr<ProjectClip> ProjectSubClip::getMasterClip() const
{
    return m_masterClip;
}

ClipType::ProducerType ProjectSubClip::clipType() const
{
    return m_masterClip->clipType();
}

bool ProjectSubClip::hasAudioAndVideo() const
{
    return m_masterClip->hasAudioAndVideo();
}

void ProjectSubClip::getThumbFromPercent(int percent)
{
    // extract a maximum of 50 frames for bin preview
    percent += percent%2;
    int framePos = (m_outPoint - m_inPoint) * percent / 100;
    if (ThumbnailCache::get()->hasThumbnail(m_parentClipId, m_inPoint + framePos)) {
        setThumbnail(ThumbnailCache::get()->getThumbnail(m_parentClipId, m_inPoint + framePos));
    } else {
        // Generate percent thumbs
        int id;
        if (pCore->jobManager()->hasPendingJob(m_parentClipId, AbstractClipJob::CACHEJOB, &id)) {
        } else {
            pCore->jobManager()->startJob<CacheJob>({m_parentClipId}, -1, QString(), 150, 25, m_inPoint, m_outPoint);
        }
    }
}
