/***************************************************************************
                          geomeytrval.h  -  description
                             -------------------
    begin                : 03 Aug 2008
    copyright            : (C) 2008 by Marco Gittler
    email                : g.marco@freenet.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef GEOMETRYVAL_H
#define GEOMETRYVAL_H


#include "ui_geometryval_ui.h"
#include <QWidget>
#include <QDomElement>

//class QGraphicsScene;
class GraphicsSceneRectMove;
class QGraphicsRectItem;
class QMouseEvent;

class Geometryval : public QWidget {
public:
    Geometryval(QWidget* parent = 0);
    QDomElement getParamDesc();
private:
    Ui::Geometryval ui;
    //QGraphicsScene* scene;
    GraphicsSceneRectMove *scene;
    QDomElement param;
    QGraphicsRectItem *paramRect;
public slots:
    void setupParam(const QDomElement&, const QString& paramName, int, int);
protected:
    virtual void mouseMoveEvent(QMouseEvent *event);
signals:
    void parameterChanged();
};

#endif
