/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#ifndef PICSTATUS_H
#define PICSTATUS_H

#include <qdialog.h>
#include <qpushbutton.h>
#include <qtable.h>
#include <qlayout.h>
#include <qtooltip.h>
#include <qcheckbox.h>
#include <qheader.h>
#include <qptrlist.h>
#include <qvaluelist.h>
#include "scribusdoc.h"
#include "scribusview.h"

class PicStatus : public QDialog
{ 
    Q_OBJECT

public:
    PicStatus(QWidget* parent, ScribusDoc *docu, ScribusView *viewi);
    ~PicStatus() {};

    QTable* PicTable;
    QHeader *Header;
    QPushButton* CancelB;
    QPushButton* OkB;
    ScribusDoc *doc;
    ScribusView *view;
    int Zeilen;
    QPtrList<QCheckBox> FlagsPic;
    QValueList<uint> ItemNrs;

private slots:
    void GotoPic();
    void SearchPic();
    void PrintPic();

protected:
    QVBoxLayout* PicStatusLayout;
    QHBoxLayout* Layout2;

signals:
	void GotoSeite(int);
};

#endif // PICSTATUS_H
