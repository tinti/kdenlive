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

#include "clipcreator.hpp"
#include "bin/bin.h"
#include "core.h"
#include "doc/kdenlivedoc.h"
#include "kdenlivesettings.h"
#include "klocalizedstring.h"
#include "macros.hpp"
#include "mainwindow.h"
#include "projectitemmodel.h"
#include "titler/titledocument.h"
#include "utils/devices.hpp"
#include "xml/xml.hpp"
#include <KMessageBox>
#include <QApplication>
#include <QDomDocument>
#include <QMimeDatabase>
#include <QProgressDialog>
#include <utility>

namespace {
QDomElement createProducer(QDomDocument &xml, ClipType::ProducerType type, const QString &resource, const QString &name, int duration, const QString &service)
{
    QDomElement prod = xml.createElement(QStringLiteral("producer"));
    xml.appendChild(prod);
    prod.setAttribute(QStringLiteral("type"), (int)type);
    prod.setAttribute(QStringLiteral("in"), QStringLiteral("0"));
    prod.setAttribute(QStringLiteral("length"), duration);
    std::unordered_map<QString, QString> properties;
    properties[QStringLiteral("resource")] = resource;
    if (!name.isEmpty()) {
        properties[QStringLiteral("kdenlive:clipname")] = name;
    }
    if (!service.isEmpty()) {
        properties[QStringLiteral("mlt_service")] = service;
    }
    Xml::addXmlProperties(prod, properties);
    return prod;
}

} // namespace

QString ClipCreator::createTitleClip(const std::unordered_map<QString, QString> &properties, int duration, const QString &name, const QString &parentFolder,
                                     const std::shared_ptr<ProjectItemModel> &model)
{
    QDomDocument xml;
    auto prod = createProducer(xml, ClipType::Text, QString(), name, duration, QStringLiteral("kdenlivetitle"));
    Xml::addXmlProperties(prod, properties);

    QString id;
    bool res = model->requestAddBinClip(id, xml.documentElement(), parentFolder, i18n("Create title clip"));
    return res ? id : QStringLiteral("-1");
}

QString ClipCreator::createColorClip(const QString &color, int duration, const QString &name, const QString &parentFolder,
                                     const std::shared_ptr<ProjectItemModel> &model)
{
    QDomDocument xml;

    auto prod = createProducer(xml, ClipType::Color, color, name, duration, QStringLiteral("color"));

    QString id;
    bool res = model->requestAddBinClip(id, xml.documentElement(), parentFolder, i18n("Create color clip"));
    return res ? id : QStringLiteral("-1");
}

QString ClipCreator::createClipFromFile(const QString &path, const QString &parentFolder, const std::shared_ptr<ProjectItemModel> &model, Fun &undo, Fun &redo,
                                        const std::function<void(const QString &)> &readyCallBack)
{
    QDomDocument xml;
    QMimeDatabase db;
    QMimeType type = db.mimeTypeForUrl(QUrl::fromLocalFile(path));

    qDebug() << "/////////// createClipFromFile" << path << parentFolder << path << type.name();
    QDomElement prod;
    if (type.name().startsWith(QLatin1String("image/"))) {
        int duration = pCore->currentDoc()->getFramePos(KdenliveSettings::image_duration());
        prod = createProducer(xml, ClipType::Image, path, QString(), duration, QString());
    } else if (type.inherits(QStringLiteral("application/x-kdenlivetitle"))) {
        // opening a title file
        QDomDocument txtdoc(QStringLiteral("titledocument"));
        QFile txtfile(path);
        if (txtfile.open(QIODevice::ReadOnly) && txtdoc.setContent(&txtfile)) {
            txtfile.close();
            // extract embedded images
            QDomNodeList items = txtdoc.elementsByTagName(QStringLiteral("content"));
            for (int j = 0; j < items.count(); ++j) {
                QDomElement content = items.item(j).toElement();
                if (content.hasAttribute(QStringLiteral("base64"))) {
                    QString titlesFolder = pCore->currentDoc()->projectDataFolder() + QStringLiteral("/titles/");
                    QString imgPath = TitleDocument::extractBase64Image(titlesFolder, content.attribute(QStringLiteral("base64")));
                    if (!imgPath.isEmpty()) {
                        content.setAttribute(QStringLiteral("url"), imgPath);
                        content.removeAttribute(QStringLiteral("base64"));
                    }
                }
            }
            prod = createProducer(xml, ClipType::Text, path, QString(), -1, QString());
            QString titleData = txtdoc.toString();
            prod.setAttribute(QStringLiteral("xmldata"), titleData);
        } else {
            txtfile.close();
            return QStringLiteral("-1");
        }
    } else {
        // it is a "normal" file, just use a producer
        prod = xml.createElement(QStringLiteral("producer"));
        xml.appendChild(prod);
        QMap<QString, QString> properties;
        properties.insert(QStringLiteral("resource"), path);
        Xml::addXmlProperties(prod, properties);
    }
    if (pCore->bin()->isEmpty() && (KdenliveSettings::default_profile().isEmpty() || KdenliveSettings::checkfirstprojectclip())) {
        prod.setAttribute(QStringLiteral("_checkProfile"), 1);
    }

    qDebug() << "/////////// final xml" << xml.toString();
    QString id;
    bool res = model->requestAddBinClip(id, xml.documentElement(), parentFolder, undo, redo, readyCallBack);
    return res ? id : QStringLiteral("-1");
}

bool ClipCreator::createClipFromFile(const QString &path, const QString &parentFolder, std::shared_ptr<ProjectItemModel> model)
{
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    auto id = ClipCreator::createClipFromFile(path, parentFolder, std::move(model), undo, redo);
    bool ok = (id != QStringLiteral("-1"));
    if (ok) {
        pCore->pushUndo(undo, redo, i18n("Add clip"));
    }
    return ok;
}

QString ClipCreator::createSlideshowClip(const QString &path, int duration, const QString &name, const QString &parentFolder,
                                         const std::unordered_map<QString, QString> &properties, const std::shared_ptr<ProjectItemModel> &model)
{
    QDomDocument xml;

    auto prod = createProducer(xml, ClipType::SlideShow, path, name, duration, QString());
    Xml::addXmlProperties(prod, properties);

    QString id;
    bool res = model->requestAddBinClip(id, xml.documentElement(), parentFolder, i18n("Create slideshow clip"));
    return res ? id : QStringLiteral("-1");
}

QString ClipCreator::createTitleTemplate(const QString &path, const QString &text, const QString &name, const QString &parentFolder,
                                         const std::shared_ptr<ProjectItemModel> &model)
{
    QDomDocument xml;

    // We try to retrieve duration for template
    int duration = 0;
    QDomDocument titledoc;
    QFile txtfile(path);
    if (txtfile.open(QIODevice::ReadOnly) && titledoc.setContent(&txtfile)) {
        if (titledoc.documentElement().hasAttribute(QStringLiteral("duration"))) {
            duration = titledoc.documentElement().attribute(QStringLiteral("duration")).toInt();
        } else {
            // keep some time for backwards compatibility - 26/12/12
            duration = titledoc.documentElement().attribute(QStringLiteral("out")).toInt();
        }
    }
    txtfile.close();

    // Duration not found, we fall-back to defaults
    if (duration == 0) {
        duration = pCore->currentDoc()->getFramePos(KdenliveSettings::title_duration());
    }
    auto prod = createProducer(xml, ClipType::TextTemplate, path, name, duration, QString());
    if (!text.isEmpty()) {
        prod.setAttribute(QStringLiteral("templatetext"), text);
    }

    QString id;
    bool res = model->requestAddBinClip(id, xml.documentElement(), parentFolder, i18n("Create title template"));
    return res ? id : QStringLiteral("-1");
}

bool ClipCreator::createClipsFromList(const QList<QUrl> &list, bool checkRemovable, const QString &parentFolder, const std::shared_ptr<ProjectItemModel> &model,
                                      Fun &undo, Fun &redo, bool topLevel)
{
    QScopedPointer<QProgressDialog> progressDialog;
    if (topLevel) {
        progressDialog.reset(new QProgressDialog(pCore->window()));
        progressDialog->setWindowTitle(i18n("Loading clips"));
        progressDialog->setCancelButton(nullptr);
        progressDialog->setLabelText(i18n("Importing bin clips..."));
        progressDialog->setMaximum(0);
        progressDialog->show();
        progressDialog->repaint();
        qApp->processEvents();
    }
    qDebug() << "/////////// creatclipsfromlist" << list << checkRemovable << parentFolder;
    bool created = false;
    QMimeDatabase db;
    for (const QUrl &file : list) {
        if (!QFile::exists(file.toLocalFile())) {
            continue;
        }
        QMimeType mType = db.mimeTypeForUrl(file);
        if (mType.inherits(QLatin1String("inode/directory"))) {
            // user dropped a folder, import its files
            QDir dir(file.path());
            QString folderId;
            Fun local_undo = []() { return true; };
            Fun local_redo = []() { return true; };
            bool folderCreated = pCore->projectItemModel()->requestAddFolder(folderId, dir.dirName(), parentFolder, local_undo, local_redo);
            if (!folderCreated) {
                continue;
            }
            QStringList result = dir.entryList(QDir::Files);
            QStringList subfolders = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            QList<QUrl> folderFiles;
            for (const QString &path : result) {
                QUrl url = QUrl::fromLocalFile(dir.absoluteFilePath(path));
                // Check file is of a supported type
                mType = db.mimeTypeForUrl(url);
                QString mimeAliases = mType.name();
                bool isValid = mimeAliases.contains(QLatin1String("video/"));
                if (!isValid) {
                    isValid = mimeAliases.contains(QLatin1String("audio/"));
                }
                if (!isValid) {
                    isValid = mimeAliases.contains(QLatin1String("image/"));
                }
                if (!isValid && (mType.inherits(QLatin1String("video/mlt-playlist")) || mType.inherits(QLatin1String("application/x-kdenlivetitle")))) {
                    isValid = true;
                }
                if (isValid) {
                    folderFiles.append(url);
                }
            }
            if (folderFiles.isEmpty()) {
                QList<QUrl> sublist;
                for (const QString &sub : subfolders) {
                    QUrl url = QUrl::fromLocalFile(dir.absoluteFilePath(sub));
                    if (!list.contains(url)) {
                        sublist << url;
                    }
                }
                if (!sublist.isEmpty()) {
                    // load subfolders
                    created = created || createClipsFromList(sublist, checkRemovable, folderId, model, undo, redo, false);
                }
            } else {
                bool clipsCreated = createClipsFromList(folderFiles, checkRemovable, folderId, model, local_undo, local_redo, false);
                created = true;
                if (!clipsCreated) {
                    local_undo();
                } else {
                    UPDATE_UNDO_REDO_NOLOCK(local_redo, local_undo, undo, redo)
                }
                // Check subfolders
                QList<QUrl> sublist;
                for (const QString &sub : subfolders) {
                    QUrl url = QUrl::fromLocalFile(dir.absoluteFilePath(sub));
                    if (!list.contains(url)) {
                        sublist << url;
                    }
                }
                if (!sublist.isEmpty()) {
                    // load subfolders
                    createClipsFromList(sublist, checkRemovable, folderId, model, undo, redo, false);
                }
            }
        } else {
            // file is not a directory
            if (checkRemovable && isOnRemovableDevice(file) && !isOnRemovableDevice(pCore->currentDoc()->projectDataFolder())) {
                int answer = KMessageBox::warningContinueCancel(
                    QApplication::activeWindow(),
                    i18n("Clip <b>%1</b><br /> is on a removable device, will not be available when device is unplugged or mounted at a different position. You "
                         "may want to copy it first to your hard-drive. Would you like to add it anyways?",
                         file.path()),
                    i18n("Removable device"), KStandardGuiItem::cont(), KStandardGuiItem::cancel(), QStringLiteral("confirm_removable_device"));

                if (answer == KMessageBox::Cancel) continue;
            }
            QString id = ClipCreator::createClipFromFile(file.toLocalFile(), parentFolder, model, undo, redo);
            created = created || (id != QStringLiteral("-1"));
        }
    }
    qDebug() << "/////////// creatclipsfromlist return" << created;
    return created;
}

bool ClipCreator::createClipsFromList(const QList<QUrl> &list, bool checkRemovable, const QString &parentFolder, std::shared_ptr<ProjectItemModel> model)
{
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    bool ok = ClipCreator::createClipsFromList(list, checkRemovable, parentFolder, std::move(model), undo, redo);
    if (ok) {
        pCore->pushUndo(undo, redo, i18np("Add clip", "Add clips", list.size()));
    }
    return ok;
}
