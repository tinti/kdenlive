/*
Copyright (C) 2014  Till Theato <root@ttill.de>
This file is part of kdenlive. See www.kdenlive.org.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

#ifndef PROJECTMANAGER_H
#define PROJECTMANAGER_H

#include <QObject>
#include <QUrl>
#include <QTimer>

#include <KRecentFilesAction>
#include "kdenlivecore_export.h"

#include "timeline/timeline.h"

class Project;
class KdenliveDoc;
class NotesPlugin;
class QAction;
class QUrl;
class KAutoSaveFile;

/**
 * @class ProjectManager
 * @brief Takes care of interaction with projects.
 */


class /*KDENLIVECORE_EXPORT*/ ProjectManager : public QObject
{
    Q_OBJECT

public:
    /** @brief Sets up actions to interact for project interaction (undo, redo, open, save, ...) and creates an empty project. */
    explicit ProjectManager(QObject* parent = 0);
    virtual ~ProjectManager();

    /** @brief Returns a pointer to the currently opened project. A project should always be open. */
    KdenliveDoc *current();
    Timeline *currentTimeline();

    /** @brief Store command line args for later opening. */
    void init(const QUrl &projectUrl, const QString &clipList);

    void doOpenFile(const QUrl &url, KAutoSaveFile *stale);
    void recoverFiles(const QList<KAutoSaveFile *> &staleFiles, const QUrl &originUrl);
    KRecentFilesAction *recentFilesAction();

public slots:
    void newFile(bool showProjectSettings = true, bool force = false);
    /** @brief Shows file open dialog. */
    void openFile();
    void openLastFile();
    /** @brief Load files / clips passed on the command line. */
    void slotLoadOnOpen();

    /** @brief Checks whether a URL is available to save to.
    * @return Whether the file was saved. */
    bool saveFile();

    /** @brief Shows a save file dialog for saving the project.
    * @return Whether the file was saved. */
    bool saveFileAs();

    /** @brief Set properties to match outputFileName and save the document.
     * Creates an autosave version of the output file too, at
     * ~/.kde/data/stalefiles/kdenlive/ \n
     * that will be actually written in KdenliveDoc::slotAutoSave()
    * @param outputFileName The URL to save to / The document's URL.
    * @return Whether we had success. */
    bool saveFileAs(const QString &outputFileName);
    /** @brief Close currently opened document. Returns false if something went wrong (cannot save modifications, ...). */
    bool closeCurrentDocument(bool saveChanges = true);

    /** @brief Prepares opening @param url.
    *
    * Checks if already open and whether backup exists */
    void openFile(const QUrl &url);
    
    /** @brief Start autosave timer */
    void slotStartAutoSave();

private slots:
    void slotRevert();
    /** @brief Open the project's backupdialog. */
    void slotOpenBackup(const QUrl &url = QUrl());
    /** @brief Start autosaving the document. */
    void slotAutoSave();

signals:
    void docOpened(KdenliveDoc *document);
//     void projectOpened(Project *project);

private:
    /** @brief Checks that the Kdenlive mime type is correctly installed.
    * @param open If set to true, this will return the mimetype allowed for file opening (adds .tar.gz format)
    * @return The mimetype */
    QString getMimeType(bool open = true);
    /** @brief checks if autoback files exists, recovers from it if user says yes, returns true if files were recovered. */
    bool checkForBackupFile(const QUrl &url);

    KdenliveDoc *m_project;
    Timeline *m_trackView;
    
    QTimer m_autoSaveTimer;
    
    QUrl m_startUrl;
    QString m_loadClipsOnOpen;

    QAction *m_fileRevert;
    KRecentFilesAction *m_recentFilesAction;
    NotesPlugin *m_notesPlugin;
};

#endif
