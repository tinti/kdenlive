/*
Copyright (C) 2007  Jean-Baptiste Mardelle <jb@kdenlive.org>
Copyright (C) 2014  Till Theato <root@ttill.de>
This file is part of kdenlive. See www.kdenlive.org.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

#include "mltconnection.h"
#include "core.h"
#include "kdenlivesettings.h"
#include "mainwindow.h"
#include "mlt_config.h"
#include <KUrlRequesterDialog>
#include <config-kdenlive.h>
#include <klocalizedstring.h>

#include "kdenlive_debug.h"
#include <QFile>
#include <QStandardPaths>
#include <mlt++/MltFactory.h>
#include <mlt++/MltRepository.h>

std::unique_ptr<MltConnection> MltConnection::m_self;
MltConnection::MltConnection(const QString &mltPath)
{
    // Disable VDPAU that crashes in multithread environment.
    // TODO: make configurable
    setenv("MLT_NO_VDPAU", "1", 1);
    m_repository = std::unique_ptr<Mlt::Repository>(Mlt::Factory::init());
    locateMeltAndProfilesPath(mltPath);

    // Retrieve the list of available producers.
    QScopedPointer<Mlt::Properties> producers(m_repository->producers());
    QStringList producersList;
    int nb_producers = producers->count();
    producersList.reserve(nb_producers);
    for (int i = 0; i < nb_producers; ++i) {
        producersList << producers->get_name(i);
    }
    KdenliveSettings::setProducerslist(producersList);

    refreshLumas();
}

void MltConnection::construct(const QString &mltPath)
{
    if (MltConnection::m_self) {
        qDebug() << "DEBUG: Warning : trying to open a second mlt connection";
        return;
    }
    MltConnection::m_self.reset(new MltConnection(mltPath));
}

std::unique_ptr<MltConnection> &MltConnection::self()
{
    return MltConnection::m_self;
}

void MltConnection::locateMeltAndProfilesPath(const QString &mltPath)
{
    QString profilePath = mltPath;
    // environment variables should override other settings
    if ((profilePath.isEmpty() || !QFile::exists(profilePath)) && qEnvironmentVariableIsSet("MLT_PROFILES_PATH")) {
        profilePath = qgetenv("MLT_PROFILES_PATH");
    }
    if ((profilePath.isEmpty() || !QFile::exists(profilePath)) && qEnvironmentVariableIsSet("MLT_DATA")) {
        profilePath = qgetenv("MLT_DATA") + QStringLiteral("/profiles");
    }
    if ((profilePath.isEmpty() || !QFile::exists(profilePath)) && qEnvironmentVariableIsSet("MLT_PREFIX")) {
        profilePath = qgetenv("MLT_PREFIX") + QStringLiteral("/share/mlt/profiles");
    }
#ifndef Q_OS_WIN
    // stored setting should not be considered on windows as MLT is distributed with each new Kdenlive version
    if ((profilePath.isEmpty() || !QFile::exists(profilePath)) && !KdenliveSettings::mltpath().isEmpty()) profilePath = KdenliveSettings::mltpath();
#endif
    // try to automatically guess MLT path if installed with the same prefix as kdenlive with default data path
    if (profilePath.isEmpty() || !QFile::exists(profilePath)) {
        profilePath = QDir::cleanPath(qApp->applicationDirPath() + QStringLiteral("/../share/mlt/profiles"));
    }
    // fallback to build-time definition
    if ((profilePath.isEmpty() || !QFile::exists(profilePath)) && !QStringLiteral(MLT_DATADIR).isEmpty()) {
        profilePath = QStringLiteral(MLT_DATADIR) + QStringLiteral("/profiles");
    }
    KdenliveSettings::setMltpath(profilePath);

#ifdef Q_OS_WIN
    QString exeSuffix = ".exe";
#else
    QString exeSuffix = "";
#endif
    QString meltPath;
    if (qEnvironmentVariableIsSet("MLT_PREFIX")) {
        meltPath = qgetenv("MLT_PREFIX") + QStringLiteral("/bin/melt") + exeSuffix;
    } else {
        meltPath = KdenliveSettings::rendererpath();
    }
    if (!QFile::exists(meltPath)) {
        meltPath = QDir::cleanPath(profilePath + QStringLiteral("/../../../bin/melt")) + exeSuffix;
        if (!QFile::exists(meltPath)) {
            meltPath = QStandardPaths::findExecutable("melt");
            if (meltPath.isEmpty()) {
                meltPath = QStandardPaths::findExecutable("mlt-melt");
            }
        }
    }
    KdenliveSettings::setRendererpath(meltPath);

    if (meltPath.isEmpty() && !qEnvironmentVariableIsSet("MLT_TESTS")) {
        // Cannot find the MLT melt renderer, ask for location
        QScopedPointer<KUrlRequesterDialog> getUrl(
            new KUrlRequesterDialog(QUrl(), i18n("Cannot find the melt program required for rendering (part of MLT)"), pCore->window()));
        if (getUrl->exec() == QDialog::Rejected) {
            ::exit(0);
        } else {
            meltPath = getUrl->selectedUrl().toLocalFile();
            if (meltPath.isEmpty()) {
                ::exit(0);
            } else {
                KdenliveSettings::setRendererpath(meltPath);
            }
        }
    }
    if (profilePath.isEmpty()) {
        profilePath = QDir::cleanPath(meltPath + QStringLiteral("/../../share/mlt/profiles"));
        KdenliveSettings::setMltpath(profilePath);
    }
    QStringList profilesFilter;
    profilesFilter << QStringLiteral("*");
    QStringList profilesList = QDir(profilePath).entryList(profilesFilter, QDir::Files);
    if (profilesList.isEmpty()) {
        // Cannot find MLT path, try finding melt
        if (!meltPath.isEmpty()) {
            if (meltPath.contains(QLatin1Char('/'))) {
                profilePath = meltPath.section(QLatin1Char('/'), 0, -2) + QStringLiteral("/share/mlt/profiles");
            } else {
                profilePath = qApp->applicationDirPath() + QStringLiteral("/share/mlt/profiles");
            }
            profilePath = QStringLiteral("/usr/local/share/mlt/profiles");
            KdenliveSettings::setMltpath(profilePath);
            profilesList = QDir(profilePath).entryList(profilesFilter, QDir::Files);
        }
        if (profilesList.isEmpty()) {
            // Cannot find the MLT profiles, ask for location
            QScopedPointer<KUrlRequesterDialog> getUrl(
                new KUrlRequesterDialog(QUrl::fromLocalFile(profilePath), i18n("Cannot find your MLT profiles, please give the path"), pCore->window()));
            getUrl->urlRequester()->setMode(KFile::Directory);
            if (getUrl->exec() == QDialog::Rejected) {
                ::exit(0);
            } else {
                profilePath = getUrl->selectedUrl().toLocalFile();
                if (profilePath.isEmpty()) {
                    ::exit(0);
                } else {
                    KdenliveSettings::setMltpath(profilePath);
                    profilesList = QDir(profilePath).entryList(profilesFilter, QDir::Files);
                }
            }
        }
    }
    qCDebug(KDENLIVE_LOG) << "MLT profiles path: " << KdenliveSettings::mltpath();
    // Parse again MLT profiles to build a list of available video formats
    if (profilesList.isEmpty()) {
        locateMeltAndProfilesPath();
    }
}

std::unique_ptr<Mlt::Repository> &MltConnection::getMltRepository()
{
    return m_repository;
}

void MltConnection::refreshLumas()
{
    // Check for Kdenlive installed luma files, add empty string at start for no luma
    QStringList fileFilters;
    MainWindow::m_lumaFiles.clear();
    fileFilters << QStringLiteral("*.png") << QStringLiteral("*.pgm");
    QStringList customLumas = QStandardPaths::locateAll(QStandardPaths::AppDataLocation, QStringLiteral("lumas"), QStandardPaths::LocateDirectory);
    customLumas.append(QString(mlt_environment("MLT_DATA")) + QStringLiteral("/lumas"));
    for (const QString &folder : customLumas) {
        QDir topDir(folder);
        QStringList folders = topDir.entryList(QDir::AllDirs | QDir::NoDotAndDotDot);
        QString format;
        for (const QString &f : folders) {
            QStringList imagefiles;
            QDir dir(topDir.absoluteFilePath(f));
            if (f == QLatin1String("HD")) {
                format = QStringLiteral("16_9");
            } else {
                format = f;
            }
            QStringList filesnames = dir.entryList(fileFilters, QDir::Files);
            if (MainWindow::m_lumaFiles.contains(format)) {
                imagefiles = MainWindow::m_lumaFiles.value(format);
            }
            for (const QString &fname : filesnames) {
                imagefiles.append(dir.absoluteFilePath(fname));
            }
            MainWindow::m_lumaFiles.insert(format, imagefiles);
        }
    }
}
