/***************************************************************************
  classifier.h
  Raster classification using decision tree
  -------------------
  begin                : Mar 22, 2011
  copyright            : (C) 2011 by Alexander Bruy
  email                : alexander.bruy@gmail.com

 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef CLASSIFIER_H
#define CLASSIFIER_H

#include <QObject>

#include "../qgisplugin.h"

class QAction;
class QToolBar;

class QgisInterface;

class Classifier: public QObject, public QgisPlugin
{
    Q_OBJECT
  public:
    Classifier( QgisInterface* iface );
    virtual ~Classifier();

  public slots:
    //! init the gui
    virtual void initGui();
    //! Show the main dialog
    void showMainDialog();
    //! Show the about dialog
    void showAboutDialog();
    //! unload the plugin
    void unload();
    //! show the help document
    void help();

  private:
    int mPluginType;

    //! Pointer to the QGIS interface object
    QgisInterface *mIface;

    //! Pointers to the actions
    QAction * mActionClassify;
    QAction * mActionAbout;
};

#endif //CLASSIFIER_H
