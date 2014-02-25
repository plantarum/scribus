/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/

#include <QByteArray>
#include <QCursor>
#include <QDrag>
#include <QFile>
#include <QList>
#include <QMimeData>
#include <QRegExp>
#include <QStack>
#include <QTextDocument>
#include <QDebug>

#include <cstdlib>

#include "color.h"
#include "commonstrings.h"
#include "ui/customfdialog.h"
#include "fileloader.h"
#include "importvsd.h"
#include "loadsaveplugin.h"
#include "ui/missing.h"
#include "ui/multiprogressdialog.h"
#include "pagesize.h"
#include "prefscontext.h"
#include "prefsfile.h"
#include "prefsmanager.h"
#include "prefstable.h"
#include "ui/propertiespalette.h"
#include "rawimage.h"
#include "scclocale.h"
#include "sccolorengine.h"
#include "scconfig.h"
#include "scmimedata.h"
#include "scpaths.h"
#include "scpattern.h"
#include "scribus.h"
#include "scribusXml.h"
#include "scribuscore.h"
#include "sctextstream.h"
#include "selection.h"
#include "undomanager.h"
#include "util.h"
#include "util_formats.h"
#include "util_icon.h"
#include "util_math.h"

extern SCRIBUS_API ScribusQApp * ScQApp;

RawVsdPainter::RawVsdPainter(): libwpg::WPGPaintInterface()
{
	CurrColorFill = "Black";
	CurrFillShade = 100.0;
	CurrColorStroke = "Black";
	CurrStrokeShade = 100.0;
	CurrStrokeTrans = 0.0;
	CurrFillTrans = 0.0;
	Coords.resize(0);
	Coords.svgInit();
	LineW = 1.0;
	lineJoin = Qt::MiterJoin;
	lineEnd = Qt::FlatCap;
	fillrule = true;
	gradientAngle = 0.0;
	isGradient = false;
	lineSpSet = false;
	currentGradient = VGradient(VGradient::linear);
	currentGradient.clearStops();
	currentGradient.setRepeatMethod( VGradient::none );
	dashArray.clear();
	firstPage = true;
	actPage = 0;
	actTextItem = NULL;
	doProcessing = true;
}

void RawVsdPainter::startGraphics(const ::WPXPropertyList &propList)
{
	if (propList["svg:width"])
		docWidth = valueAsPoint(propList["svg:width"]);
	if (propList["svg:height"])
		docHeight = valueAsPoint(propList["svg:height"]);
	if (importerFlags & LoadSavePlugin::lfCreateDoc)
	{
		if (!firstPage)
		{
			m_Doc->addPage(actPage);
			m_Doc->setActiveLayer(baseLayer);
		}
		else
			baseLayer = m_Doc->activeLayerName();
		m_Doc->setPageSize("Custom");
		m_Doc->currentPage()->setInitialWidth(docWidth);
		m_Doc->currentPage()->setInitialHeight(docHeight);
		m_Doc->currentPage()->setWidth(docWidth);
		m_Doc->currentPage()->setHeight(docHeight);
		m_Doc->currentPage()->MPageNam = CommonStrings::trMasterPageNormal;
		m_Doc->currentPage()->m_pageSize = "Custom";
		m_Doc->reformPages(true);
		baseX = m_Doc->currentPage()->xOffset();
		baseY = m_Doc->currentPage()->yOffset();
	}
	firstPage = false;
	actPage++;
}

void RawVsdPainter::endGraphics()
{
	if (importerFlags & LoadSavePlugin::lfCreateThumbnail)
		doProcessing = false;
//	qDebug() << "endGraphics";
//  printf("RawPainter::endGraphics\n");
}

void RawVsdPainter::startLayer(const ::WPXPropertyList &propList)
{
	if (!doProcessing)
		return;
	FPointArray clip;
	if (propList["svg:clip-path"])
	{
		QString svgString = QString(propList["svg:clip-path"]->getStr().cstr());
		clip.resize(0);
		clip.svgInit();
		svgString.replace(",", ".");
		clip.parseSVG(svgString);
		QTransform m;
		m.scale(72.0, 72.0);
		clip.map(m);
	}
	QList<PageItem*> gElements;
	groupEntry gr;
	gr.clip = clip.copy();
	gr.Items = gElements;
	groupStack.push(gr);
}

void RawVsdPainter::endLayer()
{
	if (!doProcessing)
		return;
	if (groupStack.count() != 0)
	{
		PageItem *ite;
		groupEntry gr = groupStack.pop();
		QList<PageItem*> gElements = gr.Items;
		tmpSel->clear();
		if (gElements.count() > 0)
		{
			for (int dre = 0; dre < gElements.count(); ++dre)
			{
				tmpSel->addItem(gElements.at(dre), true);
				Elements->removeAll(gElements.at(dre));
			}
			ite = m_Doc->groupObjectsSelection(tmpSel);
			ite->setTextFlowMode(PageItem::TextFlowUsesBoundingBox);
			if (!gr.clip.isEmpty())
			{
				double oldX = ite->xPos();
				double oldY = ite->yPos();
				double oldW = ite->width();
				double oldH = ite->height();
				double oldgW = ite->groupWidth;
				double oldgH = ite->groupHeight;
				ite->PoLine = gr.clip.copy();
				ite->PoLine.translate(baseX, baseY);
				FPoint xy = getMinClipF(&ite->PoLine);
				ite->setXYPos(xy.x(), xy.y(), true);
				ite->PoLine.translate(-xy.x(), -xy.y());
				FPoint wh = getMaxClipF(&ite->PoLine);
				ite->setWidthHeight(wh.x(),wh.y());
				ite->groupWidth = oldgW * (ite->width() / oldW);
				ite->groupHeight = oldgH * (ite->height() / oldH);
				double dx = (ite->xPos() - oldX) / (ite->width() / ite->groupWidth);
				double dy = (ite->yPos() - oldY) / (ite->height() / ite->groupHeight);
				for (int em = 0; em < ite->groupItemList.count(); ++em)
				{
					PageItem* embedded = ite->groupItemList.at(em);
					embedded->moveBy(-dx, -dy, true);
					m_Doc->setRedrawBounding(embedded);
					embedded->OwnPage = m_Doc->OnPage(embedded);
				}
				ite->ClipEdited = true;
				ite->OldB2 = ite->width();
				ite->OldH2 = ite->height();
				ite->Clip = FlattenPath(ite->PoLine, ite->Segments);
				ite->updateGradientVectors();
			}
			Elements->append(ite);
			if (groupStack.count() != 0)
				groupStack.top().Items.append(ite);
		}
		tmpSel->clear();
	}
}

void RawVsdPainter::startEmbeddedGraphics(const ::WPXPropertyList &propList)
{
	if (!doProcessing)
		return;
	qDebug() << "startEmbeddedGraphics";
//  printf("RawPainter::startEmbeddedGraphics (%s)\n", getPropString(propList).cstr());
}

void RawVsdPainter::endEmbeddedGraphics()
{
	if (!doProcessing)
		return;
//	qDebug() << "endEmbeddedGraphics";
//  printf("RawPainter::endEmbeddedGraphics \n");
}

void RawVsdPainter::setStyle(const ::WPXPropertyList &propList, const ::WPXPropertyListVector &gradient)
{
	if (!doProcessing)
		return;
	m_style.clear();
	m_style = propList;
	isGradient = false;
	if(propList["draw:fill"] && propList["draw:fill"]->getStr() == "none")
		CurrColorFill = CommonStrings::None;
	else if(propList["draw:fill"] && propList["draw:fill"]->getStr() == "solid")
	{
		if (propList["draw:fill-color"])
		{
			CurrColorFill = parseColor(QString(propList["draw:fill-color"]->getStr().cstr()));
			if(propList["draw:opacity"])
				CurrFillTrans = 1.0 - qMin(1.0, qMax(fromPercentage(QString(propList["draw:opacity"]->getStr().cstr())), 0.0));
		}
	}
	if(propList["draw:fill"] && propList["draw:fill"]->getStr() == "gradient")
	{
		double angle = 0;
		if (propList["draw:angle"])
			angle = propList["draw:angle"]->getDouble();
		if (gradient.count() > 1)
		{
			double opacity = 1.0;
			currentGradient = VGradient(VGradient::linear);
			currentGradient.clearStops();
			currentGradient.setRepeatMethod( VGradient::none );
			double dr = 1.0 / static_cast<double>(gradient.count());
			for (unsigned c = 0; c < gradient.count(); c++)
			{
				WPXPropertyList grad = gradient[c];
				if (grad["svg:stop-color"])
				{
					QString stopName = parseColor(QString(grad["svg:stop-color"]->getStr().cstr()));
					double rampPoint = dr * c;
					if(grad["svg:offset"])
						rampPoint = fromPercentage(QString(grad["svg:offset"]->getStr().cstr()));
					const ScColor& gradC = m_Doc->PageColors[stopName];
					if(grad["svg:stop-opacity"])
						opacity = qMin(1.0, qMax(fromPercentage(QString(grad["svg:stop-opacity"]->getStr().cstr())), 0.0));
					currentGradient.addStop( ScColorEngine::getRGBColor(gradC, m_Doc), rampPoint, 0.5, opacity, stopName, 100 );
					if (c == 0)
					{
						gradColor1Str = stopName;
						gradColor1 = ScColorEngine::getRGBColor(gradC, m_Doc);
						gradColor1Trans = opacity;
					}
					else
					{
						gradColor2Str = stopName;
						gradColor2 = ScColorEngine::getRGBColor(gradC, m_Doc);
						gradColor2Trans = opacity;
					}
				}
			}
			if (currentGradient.Stops() > 1)
				isGradient = true;
		}
		else
		{
			gradColor1Str = "Black";
			gradColor2Str = "Black";
			if (propList["draw:start-color"])
				gradColor2Str = parseColor(QString(propList["draw:start-color"]->getStr().cstr()));
			if (propList["draw:end-color"])
				gradColor1Str = parseColor(QString(propList["draw:end-color"]->getStr().cstr()));
			double opacity = 1.0;
			currentGradient = VGradient(VGradient::linear);
			currentGradient.clearStops();
			currentGradient.setRepeatMethod( VGradient::none );

			if (propList["draw:style"])
			{
				if (QString(propList["draw:style"]->getStr().cstr()) == "axial")
				{
					currentGradient.addStop( ScColorEngine::getRGBColor(m_Doc->PageColors[gradColor1Str], m_Doc), 0.0, 0.5, opacity, gradColor1Str, 100 );
					currentGradient.addStop( ScColorEngine::getRGBColor(m_Doc->PageColors[gradColor2Str], m_Doc), 0.5, 0.5, opacity, gradColor2Str, 100 );
					currentGradient.addStop( ScColorEngine::getRGBColor(m_Doc->PageColors[gradColor1Str], m_Doc), 1.0, 0.5, opacity, gradColor1Str, 100 );
				}
				else
				{
					currentGradient.addStop( ScColorEngine::getRGBColor(m_Doc->PageColors[gradColor1Str], m_Doc), 0.0, 0.5, opacity, gradColor1Str, 100 );
					currentGradient.addStop( ScColorEngine::getRGBColor(m_Doc->PageColors[gradColor2Str], m_Doc), 1.0, 0.5, opacity, gradColor2Str, 100 );
				}
				isGradient = true;
			}
		}
	}
	if(propList["svg:fill-rule"])
	{
		if (QString(propList["svg:fill-rule"]->getStr().cstr()) == "nonzero")
			fillrule = false;
		else
			fillrule = true;
	}
	if (propList["svg:stroke-width"])
		LineW = valueAsPoint(propList["svg:stroke-width"]);
	if (propList["draw:stroke"])
	{
		if (propList["draw:stroke"]->getStr() == "none")
			CurrColorStroke = CommonStrings::None;
		else if ((propList["draw:stroke"]->getStr() == "solid") || (propList["draw:stroke"]->getStr() == "dash"))
		{
			if (propList["svg:stroke-color"])
			{
				CurrColorStroke = parseColor(QString(propList["svg:stroke-color"]->getStr().cstr()));
				if(propList["svg:stroke-opacity"])
					CurrStrokeTrans = 1.0 - qMin(1.0, qMax(fromPercentage(QString(propList["svg:stroke-opacity"]->getStr().cstr())), 0.0));
			}
			if (propList["draw:stroke"]->getStr() == "dash")
			{
				dashArray.clear();
				double gap = LineW;
				if (propList["draw:distance"])
					gap = valueAsPoint(propList["draw:distance"]);
				int dots1 = 0;
				if (propList["draw:dots1"])
					dots1 = propList["draw:dots1"]->getInt();
				double dots1len = LineW;
				if (propList["draw:dots1-length"])
					dots1len = valueAsPoint(propList["draw:dots1-length"]);
				int dots2 = 0;
				if (propList["draw:dots2"])
					dots2 = propList["draw:dots2"]->getInt();
				double dots2len = LineW;
				if (propList["draw:dots2-length"])
					dots2len = valueAsPoint(propList["draw:dots2-length"]);
				for (int i = 0; i < dots1; i++)
				{
					dashArray << qMax(dots1len, 0.1) << qMax(gap, 0.1);
				}
				for (int j = 0; j < dots2; j++)
				{
					dashArray << qMax(dots2len, 0.1) << qMax(gap, 0.1);
				}
			}
			else
				dashArray.clear();
		}
	}
	if (propList["svg:stroke-linecap"])
	{
		QString params = QString(propList["svg:stroke-linecap"]->getStr().cstr());
		if( params == "butt" )
			lineEnd = Qt::FlatCap;
		else if( params == "round" )
			lineEnd = Qt::RoundCap;
		else if( params == "square" )
			lineEnd = Qt::SquareCap;
		else
			lineEnd = Qt::FlatCap;
	}
	if (propList["svg:stroke-linejoin"])
	{
		QString params = QString(propList["svg:stroke-linejoin"]->getStr().cstr());
		if( params == "miter" )
			lineJoin = Qt::MiterJoin;
		else if( params == "round" )
			lineJoin = Qt::RoundJoin;
		else if( params == "bevel" )
			lineJoin = Qt::BevelJoin;
		else
			lineJoin = Qt::MiterJoin;
	}
//	qDebug() << "setStyle";
//  printf("RawPainter::setStyle(%s, gradient: (%s))\n", getPropString(propList).cstr(), getPropString(gradient).cstr());
}

void RawVsdPainter::drawRectangle(const ::WPXPropertyList &propList)
{
	if (!doProcessing)
		return;
	if (propList["svg:x"] && propList["svg:y"] && propList["svg:width"] && propList["svg:height"])
	{
		double x = valueAsPoint(propList["svg:x"]);
		double y = valueAsPoint(propList["svg:y"]);
		double w = valueAsPoint(propList["svg:width"]);
		double h = valueAsPoint(propList["svg:height"]);
		int z = m_Doc->itemAdd(PageItem::Polygon, PageItem::Rectangle, baseX + x, baseY + y, w, h, LineW, CurrColorFill, CurrColorStroke, true);
		PageItem *ite = m_Doc->Items->at(z);
		finishItem(ite);
		applyFill(ite);
		if (CurrColorFill != CommonStrings::None)
			applyShadow(ite);
	}
//	qDebug() << "drawRectangle";
//  printf("RawPainter::drawRectangle (%s)\n", getPropString(propList).cstr());
}

void RawVsdPainter::drawEllipse(const ::WPXPropertyList &propList)
{
	if (!doProcessing)
		return;
	if (propList["svg:x"] && propList["svg:y"] && propList["svg:width"] && propList["svg:height"])
	{
		double x = valueAsPoint(propList["svg:x"]);
		double y = valueAsPoint(propList["svg:y"]);
		double w = valueAsPoint(propList["svg:width"]);
		double h = valueAsPoint(propList["svg:height"]);
		int z = m_Doc->itemAdd(PageItem::Polygon, PageItem::Ellipse, baseX + x, baseY + y, w, h, LineW, CurrColorFill, CurrColorStroke, true);
		PageItem *ite = m_Doc->Items->at(z);
		finishItem(ite);
		applyFill(ite);
		if (CurrColorFill != CommonStrings::None)
			applyShadow(ite);
	}
//	qDebug() << "drawEllipse";
//  printf("RawPainter::drawEllipse (%s)\n", getPropString(propList).cstr());
}

void RawVsdPainter::drawPolyline(const ::WPXPropertyListVector &vertices)
{
	if (!doProcessing)
		return;
	Coords.resize(0);
	Coords.svgInit();
	PageItem *ite;
	Coords.svgMoveTo(valueAsPoint(vertices[0]["svg:x"]), valueAsPoint(vertices[0]["svg:y"]));
	for(unsigned i = 1; i < vertices.count(); i++)
	{
		Coords.svgLineTo(valueAsPoint(vertices[i]["svg:x"]), valueAsPoint(vertices[i]["svg:y"]));
	}
	if (Coords.size() > 0)
	{
		int z = m_Doc->itemAdd(PageItem::PolyLine, PageItem::Unspecified, baseX, baseY, 10, 10, LineW, CommonStrings::None, CurrColorStroke, true);
		ite = m_Doc->Items->at(z);
		ite->PoLine = Coords.copy();
		finishItem(ite);
		applyArrows(ite);
	}
}

void RawVsdPainter::drawPolygon(const ::WPXPropertyListVector &vertices)
{
	if (!doProcessing)
		return;
	if(vertices.count() < 2)
		return;
	Coords.resize(0);
	Coords.svgInit();
	PageItem *ite;
	int z;
	Coords.svgMoveTo(valueAsPoint(vertices[0]["svg:x"]), valueAsPoint(vertices[0]["svg:y"]));
	for(unsigned i = 1; i < vertices.count(); i++)
	{
		Coords.svgLineTo(valueAsPoint(vertices[i]["svg:x"]), valueAsPoint(vertices[i]["svg:y"]));
	}
	Coords.svgClosePath();
	if (Coords.size() > 0)
	{
		if(m_style["draw:fill"] && m_style["draw:fill"]->getStr() == "bitmap" && m_style["style:repeat"] && m_style["style:repeat"]->getStr() == "stretch")
		{
		  if (m_style["draw:fill-image"] && m_style["libwpg:mime-type"])
		  {
			  QByteArray ba(m_style["draw:fill-image"]->getStr().cstr());
			  QByteArray imageData = QByteArray::fromBase64(ba);
			  QString imgExt = "";
			  if (m_style["libwpg:mime-type"]->getStr() == "image/png")
				  imgExt = "png";
			  else if (m_style["libwpg:mime-type"]->getStr() == "image/jpeg")
				  imgExt = "jpg";
			  else if (m_style["libwpg:mime-type"]->getStr() == "image/bmp")
				  imgExt = "bmp";
			  else if (m_style["libwpg:mime-type"]->getStr() == "image/pict")
				  imgExt = "pict";
			  else if (m_style["libwpg:mime-type"]->getStr() == "image/tiff")
				  imgExt = "tif";
			  if (!imgExt.isEmpty())
			  {
				  z = m_Doc->itemAdd(PageItem::ImageFrame, PageItem::Unspecified, baseX, baseY, 10, 10, LineW, CurrColorFill, CurrColorStroke, true);
				  ite = m_Doc->Items->at(z);
				  ite->PoLine = Coords.copy();
				  finishItem(ite);
				  insertImage(ite, imgExt, imageData);
			  }
			  else if (m_style["libwpg:mime-type"]->getStr() == "image/wmf")
			  {
				  imgExt = "wmf";
				  QTemporaryFile *tempFile = new QTemporaryFile(QDir::tempPath() + "/scribus_temp_vsd_XXXXXX." + imgExt);
				  if (tempFile->open())
				  {
					  tempFile->write(imageData);
					  QString fileName = getLongPathName(tempFile->fileName());
					  tempFile->close();
					  FileLoader *fileLoader = new FileLoader(fileName);
					  int testResult = fileLoader->testFile();
					  delete fileLoader;
					  if (testResult != -1)
					  {
						  const FileFormat * fmt = LoadSavePlugin::getFormatById(testResult);
						  if( fmt )
						  {
							fmt->setupTargets(m_Doc, 0, 0, 0, &(PrefsManager::instance()->appPrefs.fontPrefs.AvailFonts));
							fmt->loadFile(fileName, LoadSavePlugin::lfUseCurrentPage|LoadSavePlugin::lfInteractive|LoadSavePlugin::lfScripted);
							if (m_Doc->m_Selection->count() > 0)
							{
								ite = m_Doc->groupObjectsSelection();
								QPainterPath ba = Coords.toQPainterPath(true);
								QRectF baR = ba.boundingRect();
								ite->setXYPos(baseX + baR.x(), baseY + baR.y(), true);
								ite->setWidthHeight(baR.width(), baR.height(), true);
								FPoint tp2(getMinClipF(&Coords));
								Coords.translate(-tp2.x(), -tp2.y());
								ite->PoLine = Coords.copy();
								finishItem(ite);
							}
						  }
					  }
				  }
				  delete tempFile;
			  }
		  }
		}
		else
		{
			z = m_Doc->itemAdd(PageItem::Polygon, PageItem::Unspecified, baseX, baseY, 10, 10, LineW, CurrColorFill, CurrColorStroke, true);
			ite = m_Doc->Items->at(z);
			ite->PoLine = Coords.copy();
			finishItem(ite);
			applyFill(ite);
		}
		if (CurrColorFill != CommonStrings::None)
			applyShadow(ite);
	}
}

void RawVsdPainter::drawPath(const ::WPXPropertyListVector &path)
{
	if (!doProcessing)
		return;
	bool isClosed = false;
	QString svgString = "";
	for(unsigned i=0; i < path.count(); i++)
	{
		WPXPropertyList propList = path[i];
		if (propList["libwpg:path-action"] && propList["libwpg:path-action"]->getStr() == "M")
			svgString += QString("M %1 %2 ").arg(valueAsPoint(propList["svg:x"])).arg(valueAsPoint(propList["svg:y"]));
		else if (propList["libwpg:path-action"] && propList["libwpg:path-action"]->getStr() == "L")
			svgString += QString("L %1 %2 ").arg(valueAsPoint(propList["svg:x"])).arg(valueAsPoint(propList["svg:y"]));
		else if (propList["libwpg:path-action"] && propList["libwpg:path-action"]->getStr() == "C")
			svgString += QString("C %1 %2 %3 %4 %5 %6 ").arg(valueAsPoint(propList["svg:x1"])).arg(valueAsPoint(propList["svg:y1"])).arg(valueAsPoint(propList["svg:x2"])).arg(valueAsPoint(propList["svg:y2"])).arg(valueAsPoint(propList["svg:x"])).arg(valueAsPoint(propList["svg:y"]));
		else if (propList["libwpg:path-action"] && propList["libwpg:path-action"]->getStr() == "Q")
			svgString += QString("Q %1 %2 %3 %4 ").arg(valueAsPoint(propList["svg:x1"])).arg(valueAsPoint(propList["svg:y1"])).arg(valueAsPoint(propList["svg:x"])).arg(valueAsPoint(propList["svg:y"]));
		else if (propList["libwpg:path-action"] && propList["libwpg:path-action"]->getStr() == "A")
			svgString += QString("A %1 %2 %3 %4 %5 %6 %7") .arg(valueAsPoint(propList["svg:rx"])) .arg(valueAsPoint(propList["svg:ry"])).arg(propList["libwpg:rotate"] ? propList["libwpg:rotate"]->getDouble() : 0).arg(propList["libwpg:large-arc"] ? propList["libwpg:large-arc"]->getInt() : 1).arg(propList["libwpg:sweep"] ? propList["libwpg:sweep"]->getInt() : 1).arg(valueAsPoint(propList["svg:x"])).arg(valueAsPoint(propList["svg:y"]));
		else if ((i >= path.count()-1 && i > 2) && propList["libwpg:path-action"] && propList["libwpg:path-action"]->getStr() == "Z" )
		{
			isClosed = true;
			svgString += "Z";
		}
	}
	Coords.resize(0);
	Coords.svgInit();
	Coords.parseSVG(svgString);
	PageItem *ite;
	int z;
	if (isClosed)
	{
		if(m_style["draw:fill"] && m_style["draw:fill"]->getStr() == "bitmap" && m_style["style:repeat"] && m_style["style:repeat"]->getStr() == "stretch")
		{
		  if (m_style["draw:fill-image"] && m_style["libwpg:mime-type"])
		  {
			  QByteArray ba(m_style["draw:fill-image"]->getStr().cstr());
			  QByteArray imageData = QByteArray::fromBase64(ba);
			  QString imgExt = "";
			  if (m_style["libwpg:mime-type"]->getStr() == "image/png")
				  imgExt = "png";
			  else if (m_style["libwpg:mime-type"]->getStr() == "image/jpeg")
				  imgExt = "jpg";
			  else if (m_style["libwpg:mime-type"]->getStr() == "image/bmp")
				  imgExt = "bmp";
			  else if (m_style["libwpg:mime-type"]->getStr() == "image/pict")
				  imgExt = "pict";
			  else if (m_style["libwpg:mime-type"]->getStr() == "image/tiff")
				  imgExt = "tif";
			  if (!imgExt.isEmpty())
			  {
				  z = m_Doc->itemAdd(PageItem::ImageFrame, PageItem::Unspecified, baseX, baseY, 10, 10, LineW, CurrColorFill, CurrColorStroke, true);
				  ite = m_Doc->Items->at(z);
				  ite->PoLine = Coords.copy();
				  finishItem(ite);
				  insertImage(ite, imgExt, imageData);
			  }
			  else if (m_style["libwpg:mime-type"]->getStr() == "image/wmf")
			  {
				  imgExt = "wmf";
				  QTemporaryFile *tempFile = new QTemporaryFile(QDir::tempPath() + "/scribus_temp_vsd_XXXXXX." + imgExt);
				  tempFile->setAutoRemove(false);
				  if (tempFile->open())
				  {
					  tempFile->write(imageData);
					  QString fileName = getLongPathName(tempFile->fileName());
					  tempFile->close();
					  FileLoader *fileLoader = new FileLoader(fileName);
					  int testResult = fileLoader->testFile();
					  delete fileLoader;
					  if (testResult != -1)
					  {
						  const FileFormat * fmt = LoadSavePlugin::getFormatById(testResult);
						  if( fmt )
						  {
							  fmt->setupTargets(m_Doc, 0, 0, 0, &(PrefsManager::instance()->appPrefs.fontPrefs.AvailFonts));
							  fmt->loadFile(fileName, LoadSavePlugin::lfUseCurrentPage|LoadSavePlugin::lfInteractive|LoadSavePlugin::lfScripted);
							  if (m_Doc->m_Selection->count() > 0)
							  {
								ite = m_Doc->groupObjectsSelection();
								QPainterPath ba = Coords.toQPainterPath(true);
								QRectF baR = ba.boundingRect();
								ite->setXYPos(baseX + baR.x(), baseY + baR.y(), true);
								ite->setWidthHeight(baR.width(), baR.height(), true);
								FPoint tp2(getMinClipF(&Coords));
								Coords.translate(-tp2.x(), -tp2.y());
								ite->PoLine = Coords.copy();
								finishItem(ite);
							  }
						  }
					  }
				  }
				  delete tempFile;
			  }
		  }
		}
		else
		{
			z = m_Doc->itemAdd(PageItem::Polygon, PageItem::Unspecified, baseX, baseY, 10, 10, LineW, CurrColorFill, CurrColorStroke, true);
			ite = m_Doc->Items->at(z);
			ite->PoLine = Coords.copy();
			finishItem(ite);
			applyFill(ite);
		}
		if (CurrColorFill != CommonStrings::None)
			applyShadow(ite);
	}
	else
	{
		z = m_Doc->itemAdd(PageItem::PolyLine, PageItem::Unspecified, baseX, baseY, 10, 10, LineW, CommonStrings::None, CurrColorStroke, true);
		ite = m_Doc->Items->at(z);
		ite->PoLine = Coords.copy();
		finishItem(ite);
		applyArrows(ite);
	}
}

void RawVsdPainter::drawGraphicObject(const ::WPXPropertyList &propList, const ::WPXBinaryData &binaryData)
{
	if (!doProcessing)
		return;
	if (!propList["libwpg:mime-type"] || propList["libwpg:mime-type"]->getStr().len() <= 0)
		return;
	WPXString base64 = binaryData.getBase64Data();
	if (propList["svg:x"] && propList["svg:y"] && propList["svg:width"] && propList["svg:height"])
	{
		PageItem *ite;
		double x = valueAsPoint(propList["svg:x"]);
		double y = valueAsPoint(propList["svg:y"]);
		double w = valueAsPoint(propList["svg:width"]);
		double h = valueAsPoint(propList["svg:height"]);
		QByteArray ba(base64.cstr());
		QByteArray imageData = QByteArray::fromBase64(ba);
		QString imgExt = "";
		if (propList["libwpg:mime-type"]->getStr() == "image/png")
			imgExt = "png";
		else if (propList["libwpg:mime-type"]->getStr() == "image/jpeg")
			imgExt = "jpg";
		else if (propList["libwpg:mime-type"]->getStr() == "image/bmp")
			imgExt = "bmp";
		else if (propList["libwpg:mime-type"]->getStr() == "image/pict")
			imgExt = "pict";
		else if (propList["libwpg:mime-type"]->getStr() == "image/tiff")
			imgExt = "tif";
		if (!imgExt.isEmpty())
		{
			int z = m_Doc->itemAdd(PageItem::ImageFrame, PageItem::Rectangle, baseX + x, baseY + y, w, h, 0, CurrColorFill, CurrColorStroke, true);
			ite = m_Doc->Items->at(z);
			finishItem(ite);
			insertImage(ite, imgExt, imageData);
		}
		else
		{
			if (propList["libwpg:mime-type"]->getStr() == "image/wmf")
			{
				imgExt = "wmf";
				QTemporaryFile *tempFile = new QTemporaryFile(QDir::tempPath() + "/scribus_temp_vsd_XXXXXX." + imgExt);
				if (tempFile->open())
				{
					tempFile->write(imageData);
					QString fileName = getLongPathName(tempFile->fileName());
					tempFile->close();
					FileLoader *fileLoader = new FileLoader(fileName);
					int testResult = fileLoader->testFile();
					delete fileLoader;
					if (testResult != -1)
					{
						const FileFormat * fmt = LoadSavePlugin::getFormatById(testResult);
						if( fmt )
						{
							fmt->setupTargets(m_Doc, 0, 0, 0, &(PrefsManager::instance()->appPrefs.fontPrefs.AvailFonts));
							fmt->loadFile(fileName, LoadSavePlugin::lfUseCurrentPage|LoadSavePlugin::lfInteractive|LoadSavePlugin::lfScripted);
							if (m_Doc->m_Selection->count() > 0)
							{
								ite = m_Doc->groupObjectsSelection();
								ite->setTextFlowMode(PageItem::TextFlowUsesBoundingBox);
								Elements->append(ite);
								ite->setXYPos(baseX + x, baseY + y, true);
								ite->setWidthHeight(w, h, true);
								ite->updateClip();
							}
						}
					}
				}
				delete tempFile;
			}
		}
		applyShadow(ite);
	}
//	qDebug() << "drawGraphicObject";
//  printf("RawPainter::drawGraphicObject (%s)\n", getPropString(propList).cstr());
}

void RawVsdPainter::startTextObject(const ::WPXPropertyList &propList, const ::WPXPropertyListVector &path)
{
	if (!doProcessing)
		return;
	actTextItem = NULL;
	lineSpSet = false;
	lineSpIsPT = false;
	if (propList["svg:x"] && propList["svg:y"] && propList["svg:width"] && propList["svg:height"])
	{
		double x = valueAsPoint(propList["svg:x"]);
		double y = valueAsPoint(propList["svg:y"]);
		double w = valueAsPoint(propList["svg:width"]);
		double h = valueAsPoint(propList["svg:height"]);
		double rot = 0;
		if (propList["libwpg:rotate"])
			rot = propList["libwpg:rotate"]->getDouble();
		if ((w != 0) && (h != 0))
		{
			int z = m_Doc->itemAdd(PageItem::TextFrame, PageItem::Rectangle, baseX + x, baseY + y, w, h, 0, CurrColorFill, CurrColorStroke, true);
			PageItem *ite = m_Doc->Items->at(z);
			CurrFillTrans = 0;
			CurrStrokeTrans = 0;
			finishItem(ite);
			applyShadow(ite);
			if (rot != 0)
			{
				int rm = m_Doc->RotMode();
				m_Doc->RotMode(2);
				m_Doc->RotateItem(rot, ite);
				m_Doc->RotMode(rm);
			}
			if (propList["fo:padding-left"])
				ite->setTextToFrameDistLeft(valueAsPoint(propList["fo:padding-left"]));
			if (propList["fo:padding-right"])
				ite->setTextToFrameDistRight(valueAsPoint(propList["fo:padding-right"]));
			if (propList["fo:padding-top"])
				ite->setTextToFrameDistTop(valueAsPoint(propList["fo:padding-top"]));
			if (propList["fo:padding-bottom"])
				ite->setTextToFrameDistBottom(valueAsPoint(propList["fo:padding-bottom"]));
			if (propList["fo:column-count"])
				ite->setColumns(propList["fo:column-count"]->getInt());
			if (propList["fo:column-gap"])
				ite->setColumnGap(valueAsPoint(propList["fo:column-gap"]));
			ite->setFirstLineOffset(FLOPFontAscent);
			actTextItem = ite;
			QString pStyle = CommonStrings::DefaultParagraphStyle;
			ParagraphStyle newStyle;
			newStyle.setParent(pStyle);
			textStyle = newStyle;
		}
	}
}

void RawVsdPainter::endTextObject()
{
	if (!doProcessing)
		return;
	if (actTextItem)
		actTextItem->itemText.trim();
	actTextItem = NULL;
	lineSpSet = false;
	lineSpIsPT = false;
}

void RawVsdPainter::startTextLine(const ::WPXPropertyList &propList)
{
	if (!doProcessing)
		return;
	QString pStyle = CommonStrings::DefaultParagraphStyle;
	ParagraphStyle newStyle;
	newStyle.setParent(pStyle);
	textStyle = newStyle;
	if (propList["fo:text-align"])
	{
		QString align = QString(propList["fo:text-align"]->getStr().cstr());
		if (align == "left")
			textStyle.setAlignment(ParagraphStyle::Leftaligned);
		else if (align == "center")
			textStyle.setAlignment(ParagraphStyle::Centered);
		else if (align == "right")
			textStyle.setAlignment(ParagraphStyle::Rightaligned);
		else if (align == "justify")
			textStyle.setAlignment(ParagraphStyle::Justified);
	}
	if (propList["fo:margin-left"])
		textStyle.setLeftMargin(valueAsPoint(propList["fo:margin-left"]));
	if (propList["fo:margin-right"])
		textStyle.setRightMargin(valueAsPoint(propList["fo:margin-right"]));
	if (propList["fo:text-indent"])
		textStyle.setFirstIndent(valueAsPoint(propList["fo:text-indent"]));
	if (propList["style:drop-cap"])
	{
		textStyle.setDropCapLines(propList["style:drop-cap"]->getInt());
		textStyle.setHasDropCap(true);
	}
	if (propList["fo:margin-bottom"])
		textStyle.setGapAfter(valueAsPoint(propList["fo:margin-bottom"]));
	if (propList["fo:margin-top"])
		textStyle.setGapBefore(valueAsPoint(propList["fo:margin-top"]));
//	m_maxFontSize = textStyle.charStyle().fontSize() / 10.0;
	m_maxFontSize = 1.0;
	if (propList["fo:line-height"])
	{
		m_linespace = propList["fo:line-height"]->getDouble();
		QString lsp = QString(propList["fo:line-height"]->getStr().cstr());
		lineSpIsPT = lsp.endsWith("pt");
		lineSpSet = true;
	}
}

void RawVsdPainter::endTextLine()
{
	if (!doProcessing)
		return;
	if (actTextItem == NULL)
		return;
	int posT = actTextItem->itemText.length();
	if (posT > 0)
	{
		if ((actTextItem->itemText.text(posT - 1) != SpecialChars::PARSEP))
		{
			actTextItem->itemText.insertChars(posT, SpecialChars::PARSEP);
			actTextItem->itemText.applyStyle(posT, textStyle);
		}
	}
}

void RawVsdPainter::startTextSpan(const ::WPXPropertyList &propList)
{
	if (!doProcessing)
		return;
	if (actTextItem == NULL)
		return;
	textCharStyle = textStyle.charStyle();
	if (propList["fo:font-size"])
	{
		textCharStyle.setFontSize(valueAsPoint(propList["fo:font-size"]) * 10.0);
		m_maxFontSize = qMax(m_maxFontSize, valueAsPoint(propList["fo:font-size"]));
	}
	if (propList["fo:color"])
		textCharStyle.setFillColor(parseColor(QString(propList["fo:color"]->getStr().cstr())));
	if (propList["style:font-name"])
	{
		QString fontVari = "";
		if (propList["fo:font-weight"])
			fontVari = QString(propList["fo:font-weight"]->getStr().cstr());
		QString fontName = QString(propList["style:font-name"]->getStr().cstr());
		QString realFontName = constructFontName(fontName, fontVari);
		textCharStyle.setFont((*m_Doc->AllFonts)[realFontName]);
	}
	StyleFlag styleEffects = textCharStyle.effects();
	if (propList["style:text-underline-type"])
		styleEffects |= ScStyle_Underline;
	if (propList["style:text-position"])
	{
		if (propList["style:text-position"]->getStr() == "50% 67%")
			styleEffects |= ScStyle_Superscript;
		else
			styleEffects |= ScStyle_Subscript;
	}
	textCharStyle.setFeatures(styleEffects.featureList());
}

void RawVsdPainter::endTextSpan()
{
}

void RawVsdPainter::insertText(const ::WPXString &str)
{
	if (!doProcessing)
		return;
	if (lineSpSet)
	{
		textStyle.setLineSpacingMode(ParagraphStyle::FixedLineSpacing);
		if (lineSpIsPT)
			textStyle.setLineSpacing(m_linespace);
		else
			textStyle.setLineSpacing(m_maxFontSize * m_linespace);
	}
	else
		textStyle.setLineSpacingMode(ParagraphStyle::AutomaticLineSpacing);
	WPXString tempUTF8(str, true);
	QString actText = QString(tempUTF8.cstr());
	if (actTextItem)
	{
		int posC = actTextItem->itemText.length();
		if (actText.count() > 0)
		{
			actText.replace(QChar(10), SpecialChars::LINEBREAK);
			actText.replace(QChar(12), SpecialChars::FRAMEBREAK);
			actText.replace(QChar(30), SpecialChars::NBHYPHEN);
			actText.replace(QChar(160), SpecialChars::NBSPACE);
			QTextDocument texDoc;
			texDoc.setHtml(actText);
			actText = texDoc.toPlainText();
			actText = actText.trimmed();
			actTextItem->itemText.insertChars(posC, actText);
			actTextItem->itemText.applyStyle(posC, textStyle);
			actTextItem->itemText.applyCharStyle(posC, actText.length(), textCharStyle);
		}
	}
}

QString RawVsdPainter::constructFontName(QString fontBaseName, QString fontStyle)
{
	QString fontName = "";
	bool found = false;
	SCFontsIterator it(PrefsManager::instance()->appPrefs.fontPrefs.AvailFonts);
	for ( ; it.hasNext(); it.next())
	{
		if (fontBaseName.toLower() == it.current().family().toLower())
		{
			// found the font family, now go for the style
			QStringList slist = PrefsManager::instance()->appPrefs.fontPrefs.AvailFonts.fontMap[it.current().family()];
			slist.sort();
			if (slist.count() > 0)
			{
				for (int a = 0; a < slist.count(); a++)
				{
					if (fontStyle.toLower() == slist[a].toLower())
					{
						found = true;
						fontName = it.current().family() + " " + slist[a];
						break;
					}
				}
				if (!found)
				{
					int reInd = slist.indexOf("Regular");
					if (reInd < 0)
						fontName = it.current().family() + " " + slist[0];
					else
						fontName = it.current().family() + " " + slist[reInd];
					found = true;
				}
			}
			else
			{
				fontName = it.current().family();
				found = true;
			}
			break;
		}
	}
	if (!found)
	{
		if (importerFlags & LoadSavePlugin::lfCreateThumbnail)
			fontName = PrefsManager::instance()->appPrefs.itemToolPrefs.textFont;
		else
		{
			QString family = fontBaseName;
			if (!fontStyle.isEmpty())
				family += " " + fontStyle;
			if (!PrefsManager::instance()->appPrefs.fontPrefs.GFontSub.contains(family))
			{
				qApp->changeOverrideCursor(QCursor(Qt::ArrowCursor));
				MissingFont *dia = new MissingFont(0, family, m_Doc);
				dia->exec();
				fontName = dia->getReplacementFont();
				delete dia;
				qApp->changeOverrideCursor(QCursor(Qt::WaitCursor));
				PrefsManager::instance()->appPrefs.fontPrefs.GFontSub[family] = fontName;
			}
			else
				fontName = PrefsManager::instance()->appPrefs.fontPrefs.GFontSub[family];
		}
	}
	return fontName;
}

double RawVsdPainter::valueAsPoint(const WPXProperty *prop)
{
	double value = 0.0;
	QString str = QString(prop->getStr().cstr()).toLower();
	if (str.endsWith("in"))
		value = prop->getDouble() * 72.0;
	else
		value = prop->getDouble();
	return value;
}

double RawVsdPainter::fromPercentage( const QString &s )
{
	QString s1 = s;
	if (s1.endsWith( ";" ))
		s1.chop(1);
	if (s1.endsWith( "%" ))
	{
		s1.chop(1);
		return ScCLocale::toDoubleC(s1) / 100.0;
	}
	else
		return ScCLocale::toDoubleC(s1);
}

QColor RawVsdPainter::parseColorN( const QString &rgbColor )
{
	int r, g, b;
	keywordToRGB( rgbColor.toLower(), r, g, b );
	return QColor( r, g, b );
}

QString RawVsdPainter::parseColor( const QString &s )
{
	QColor c;
	QString ret = CommonStrings::None;
	if (s.startsWith( "rgb(" ) )
	{
		QString parse = s.trimmed();
		QStringList colors = parse.split(',', QString::SkipEmptyParts);
		QString r = colors[0].right( ( colors[0].length() - 4 ) );
		QString g = colors[1];
		QString b = colors[2].left( ( colors[2].length() - 1 ) );
		if (r.contains( "%" ))
		{
			r.chop(1);
			r = QString::number( static_cast<int>( ( static_cast<double>( 255 * ScCLocale::toDoubleC(r) ) / 100.0 ) ) );
		}
		if (g.contains( "%" ))
		{
			g.chop(1);
			g = QString::number( static_cast<int>( ( static_cast<double>( 255 * ScCLocale::toDoubleC(g) ) / 100.0 ) ) );
		}
		if (b.contains( "%" ))
		{
			b.chop(1);
			b = QString::number( static_cast<int>( ( static_cast<double>( 255 * ScCLocale::toDoubleC(b) ) / 100.0 ) ) );
		}
		c = QColor(r.toInt(), g.toInt(), b.toInt());
	}
	else
	{
		QString rgbColor = s.trimmed();
		if (rgbColor.startsWith( "#" ))
		{
			rgbColor = rgbColor.left(7);
			c.setNamedColor( rgbColor );
		}
		else
			c = parseColorN( rgbColor );
	}
	ScColor tmp;
	tmp.fromQColor(c);
	tmp.setSpotColor(false);
	tmp.setRegistrationColor(false);
	QString newColorName = "FromVSD"+c.name();
	QString fNam = m_Doc->PageColors.tryAddColor(newColorName, tmp);
	if (fNam == newColorName)
		importedColors->append(newColorName);
	ret = fNam;
	return ret;
}

void RawVsdPainter::insertImage(PageItem* ite, QString imgExt, QByteArray &imageData)
{
	QTemporaryFile *tempFile = new QTemporaryFile(QDir::tempPath() + "/scribus_temp_vsd_XXXXXX." + imgExt);
	tempFile->setAutoRemove(false);
	if (tempFile->open())
	{
		tempFile->write(imageData);
		QString fileName = getLongPathName(tempFile->fileName());
		tempFile->close();
		ite->isTempFile = true;
		ite->isInlineImage = true;
		if (m_style["draw:red"] && m_style["draw:green"] && m_style["draw:blue"])
		{
			int r = qRound(m_style["draw:red"]->getDouble() * 255);
			int g = qRound(m_style["draw:green"]->getDouble() * 255);
			int b = qRound(m_style["draw:blue"]->getDouble() * 255);
			QString colVal = QString("#%1%2%3").arg(r, 2, 16, QLatin1Char('0')).arg(g, 2, 16, QLatin1Char('0')).arg(b, 2, 16, QLatin1Char('0'));
			QString efVal = parseColor(colVal);
			efVal += "\n";
			struct ImageEffect ef;
			efVal += "100";
			ef.effectCode = ScImage::EF_COLORIZE;
			ef.effectParameters = efVal;
			ite->effectsInUse.append(ef);
		}
		if (m_style["draw:luminance"])
		{
			double per = m_style["draw:luminance"]->getDouble();
			struct ImageEffect ef;
			ef.effectCode = ScImage::EF_BRIGHTNESS;
			ef.effectParameters = QString("%1").arg(qRound((per - 0.5) * 255));
			ite->effectsInUse.append(ef);
		}
		m_Doc->loadPict(fileName, ite);
		if (m_style["libwpg:rotate"])
		{
			int rot = QString(m_style["libwpg:rotate"]->getStr().cstr()).toInt();
			ite->setImageRotation(rot);
			ite->AdjustPictScale();
		}
	}
	delete tempFile;
}

void RawVsdPainter::applyFill(PageItem* ite)
{
	if(isGradient)
	{
		QString gradMode = "linear";
		if (m_style["draw:style"])
			gradMode = QString(m_style["draw:style"]->getStr().cstr());
		if ((gradMode == "linear") || (gradMode == "axial"))
		{
			int angle = 0;
			if (m_style["draw:angle"])
				angle = qRound(m_style["draw:angle"]->getDouble());
			double h = ite->height();
			double w = ite->width();
			if (angle == 0)
				ite->setGradientVector(w / 2.0, h, w / 2.0, 0, 0, 0, 1, 0);
			else if (angle == -225)
				ite->setGradientVector(w, 0, 0, h, 0, 0, 1, 0);
			else if (angle == 45)
				ite->setGradientVector(w, h, 0, 0, 0, 0, 1, 0);
			else if (angle == 90)
				ite->setGradientVector(w, h / 2.0, 0, h / 2.0, 0, 0, 1, 0);
			else if (angle == 180)
				ite->setGradientVector(w / 2.0, 0, w / 2.0, h, 0, 0, 1, 0);
			else if (angle == 270)
				ite->setGradientVector(0, h / 2.0, w, h / 2.0, 0, 0, 1, 0);
			ite->fill_gradient = currentGradient;
			ite->GrType = 6;
		}
		else if (gradMode == "radial")
		{
			double h = ite->height();
			double w = ite->width();
			double cx = 0.0;
			double cy = 0.0;
			if (m_style["svg:cx"])
				cx = m_style["svg:cx"]->getDouble() * w;
			if (m_style["svg:cy"])
				cy = m_style["svg:cy"]->getDouble() * h;
			ite->setGradientVector(cx, cy, w, h / 2.0, cx, cy, 1, 0);
			ite->fill_gradient = currentGradient;
			ite->GrType = 7;
		}
		else if (gradMode == "square")
		{
			double cx = 0.0;
			double cy = 0.0;
			if (m_style["svg:cx"])
				cx = m_style["svg:cx"]->getDouble() * ite->width();
			if (m_style["svg:cy"])
				cy = m_style["svg:cy"]->getDouble() * ite->height();
			FPoint cp = FPoint(cx, cy);
			ite->setDiamondGeometry(FPoint(0, 0), FPoint(ite->width(), 0), FPoint(ite->width(), ite->height()), FPoint(0, ite->height()), cp);
			ite->fill_gradient.clearStops();
			QList<VColorStop*> colorStops = currentGradient.colorStops();
			for( int a = 0; a < colorStops.count() ; a++ )
			{
				ite->fill_gradient.addStop(colorStops[a]->color, 1.0 - colorStops[a]->rampPoint, colorStops[a]->midPoint, colorStops[a]->opacity, colorStops[a]->name, colorStops[a]->shade);
			}
			ite->GrType = 10;
		}
	}
	if(m_style["draw:fill"] && m_style["draw:fill"]->getStr() == "bitmap")
	{
		QByteArray ba(m_style["draw:fill-image"]->getStr().cstr());
		QByteArray imageData = QByteArray::fromBase64(ba);
		QString imgExt = "";
		if (m_style["libwpg:mime-type"]->getStr() == "image/png")
			imgExt = "png";
		else if (m_style["libwpg:mime-type"]->getStr() == "image/jpeg")
			imgExt = "jpg";
		else if (m_style["libwpg:mime-type"]->getStr() == "image/bmp")
			imgExt = "bmp";
		else if (m_style["libwpg:mime-type"]->getStr() == "image/pict")
			imgExt = "pict";
		else if (m_style["libwpg:mime-type"]->getStr() == "image/tiff")
			imgExt = "tif";
		if (!imgExt.isEmpty())
		{
			QTemporaryFile *tempFile = new QTemporaryFile(QDir::tempPath() + "/scribus_temp_vsd_XXXXXX." + imgExt);
			tempFile->setAutoRemove(false);
			if (tempFile->open())
			{
				tempFile->write(imageData);
				QString fileName = getLongPathName(tempFile->fileName());
				tempFile->close();
				ScPattern pat = ScPattern();
				pat.setDoc(m_Doc);
				int z = m_Doc->itemAdd(PageItem::ImageFrame, PageItem::Unspecified, 0, 0, 1, 1, 0, CommonStrings::None, CommonStrings::None, true);
				PageItem* newItem = m_Doc->Items->at(z);
				if (m_style["draw:red"] && m_style["draw:green"] && m_style["draw:blue"])
				{
					int r = qRound(m_style["draw:red"]->getDouble() * 255);
					int g = qRound(m_style["draw:green"]->getDouble() * 255);
					int b = qRound(m_style["draw:blue"]->getDouble() * 255);
					QString colVal = QString("#%1%2%3").arg(r, 2, 16, QLatin1Char('0')).arg(g, 2, 16, QLatin1Char('0')).arg(b, 2, 16, QLatin1Char('0'));
					QString efVal = parseColor(colVal);
					efVal += "\n";
					struct ImageEffect ef;
					efVal += "100";
					ef.effectCode = ScImage::EF_COLORIZE;
					ef.effectParameters = efVal;
					ite->effectsInUse.append(ef);
				}
				m_Doc->loadPict(fileName, newItem);
				m_Doc->Items->takeAt(z);
				newItem->isInlineImage = true;
				newItem->isTempFile = true;
				pat.width = newItem->pixm.qImage().width();
				pat.height = newItem->pixm.qImage().height();
				pat.scaleX = (72.0 / newItem->pixm.imgInfo.xres) * newItem->pixm.imgInfo.lowResScale;
				pat.scaleY = (72.0 / newItem->pixm.imgInfo.xres) * newItem->pixm.imgInfo.lowResScale;
				pat.pattern = newItem->pixm.qImage().copy();
				newItem->setWidth(pat.pattern.width());
				newItem->setHeight(pat.pattern.height());
				newItem->SetRectFrame();
				newItem->gXpos = 0.0;
				newItem->gYpos = 0.0;
				newItem->gWidth = pat.pattern.width();
				newItem->gHeight = pat.pattern.height();
				pat.items.append(newItem);
				QString patternName = "Pattern_"+ite->itemName();
				patternName = patternName.trimmed().simplified().replace(" ", "_");
				m_Doc->addPattern(patternName, pat);
				importedPatterns->append(patternName);
				ite->setPattern(patternName);
				ite->GrType = 8;
			}
			delete tempFile;
		}
	}
}

void RawVsdPainter::applyShadow(PageItem* ite)
{
	if (ite == NULL)
		return;
	if(m_style["draw:shadow"] && m_style["draw:shadow"]->getStr() == "visible")
	{
		double xp = ite->xPos();
		double yp = ite->yPos();
		double xof = 0.0;
		double yof = 0.0;
		if (m_style["draw:shadow-offset-x"])
			xof = valueAsPoint(m_style["draw:shadow-offset-x"]);
		if (m_style["draw:shadow-offset-y"])
			yof = valueAsPoint(m_style["draw:shadow-offset-y"]);
		xp += xof;
		yp += yof;
		QString shadowColor = CurrColorFill;
		double shadowTrans = 1.0;
		if (m_style["draw:shadow-color"])
		{
			shadowColor = parseColor(QString(m_style["draw:shadow-color"]->getStr().cstr()));
			if(m_style["draw:shadow-opacity"])
				shadowTrans = 1.0 - qMin(1.0, qMax(fromPercentage(QString(m_style["draw:shadow-opacity"]->getStr().cstr())), 0.0));
		}
		int z = m_Doc->itemAdd(PageItem::Polygon, PageItem::Unspecified, xp, yp, ite->width(), ite->height(), 0, shadowColor, CommonStrings::None, true);
		PageItem *nite = m_Doc->Items->takeAt(z);
		nite->PoLine = ite->PoLine.copy();
		nite->updateClip();
		nite->setFillTransparency(shadowTrans);
		nite->ClipEdited = true;
		nite->FrameType = 3;
		m_Doc->Items->takeAt(z-1);
		Elements->takeAt(z-1);
		m_Doc->Items->append(nite);
		m_Doc->Items->append(ite);
		Elements->append(nite);
		Elements->append(ite);
		if (groupStack.count() != 0)
		{
			groupStack.top().Items.takeLast();
			groupStack.top().Items.append(nite);
			groupStack.top().Items.append(ite);
		}
	}
}

void RawVsdPainter::applyArrows(PageItem* ite)
{
	if (m_style["draw:marker-end-path"])
	{
		FPointArray EndArrow;
		double EndArrowWidth;
		QString params = QString(m_style["draw:marker-end-path"]->getStr().cstr());
		EndArrowWidth = LineW;
		EndArrow.resize(0);
		EndArrow.svgInit();
		EndArrow.parseSVG(params);
		QPainterPath pa = EndArrow.toQPainterPath(true);
		QRectF br = pa.boundingRect();
		if (m_style["draw:marker-end-width"])
			EndArrowWidth = valueAsPoint(m_style["draw:marker-end-width"]);
		if (EndArrowWidth > 0)
		{
			FPoint End = ite->PoLine.point(ite->PoLine.size()-2);
			for (uint xx = ite->PoLine.size()-1; xx > 0; xx -= 2)
			{
				FPoint Vector = ite->PoLine.point(xx);
				if ((End.x() != Vector.x()) || (End.y() != Vector.y()))
				{
					double r = atan2(End.y()-Vector.y(),End.x()-Vector.x())*(180.0/M_PI);
					QPointF refP = QPointF(br.width() / 2.0, 0);
					QTransform m;
					m.translate(br.width() / 2.0, br.height() / 2.0);
					m.rotate(r + 90);
					m.translate(-br.width() / 2.0, -br.height() / 2.0);
					m.scale(EndArrowWidth / br.width(), EndArrowWidth / br.width());
					EndArrow.map(m);
					refP = m.map(refP);
					QPainterPath pa2 = EndArrow.toQPainterPath(true);
					QRectF br2 = pa2.boundingRect();
					QTransform m2;
					FPoint grOffset2(getMinClipF(&EndArrow));
					m2.translate(-grOffset2.x(), -grOffset2.y());
					EndArrow.map(m2);
					refP = m2.map(refP);
					EndArrow.translate(-refP.x(), -refP.y());
					QTransform arrowTrans;
					arrowTrans.translate(-m_Doc->currentPage()->xOffset(), -m_Doc->currentPage()->yOffset());
					arrowTrans.translate(End.x() + ite->xPos(), End.y() + ite->yPos());
					EndArrow.map(arrowTrans);
					int zE = m_Doc->itemAdd(PageItem::Polygon, PageItem::Unspecified, baseX, baseY, 10, 10, 0, CurrColorStroke, CommonStrings::None, true);
					PageItem *iteE = m_Doc->Items->at(zE);
					iteE->PoLine = EndArrow.copy();
					finishItem(iteE);
					break;
				}
			}
		}
	}
	if (m_style["draw:marker-start-path"])
	{
		FPointArray EndArrow;
		double EndArrowWidth;
		QString params = QString(m_style["draw:marker-start-path"]->getStr().cstr());
		EndArrowWidth = LineW;
		EndArrow.resize(0);
		EndArrow.svgInit();
		EndArrow.parseSVG(params);
		QPainterPath pa = EndArrow.toQPainterPath(true);
		QRectF br = pa.boundingRect();
		if (m_style["draw:marker-start-width"])
			EndArrowWidth = valueAsPoint(m_style["draw:marker-start-width"]);
		if (EndArrowWidth > 0)
		{
			FPoint Start = ite->PoLine.point(0);
			for (int xx = 1; xx < ite->PoLine.size(); xx += 2)
			{
				FPoint Vector = ite->PoLine.point(xx);
				if ((Start.x() != Vector.x()) || (Start.y() != Vector.y()))
				{
					double r = atan2(Start.y()-Vector.y(),Start.x()-Vector.x())*(180.0/M_PI);
					QPointF refP = QPointF(br.width() / 2.0, 0);
					QTransform m;
					m.translate(br.width() / 2.0, br.height() / 2.0);
					m.rotate(r + 90);
					m.translate(-br.width() / 2.0, -br.height() / 2.0);
					m.scale(EndArrowWidth / br.width(), EndArrowWidth / br.width());
					EndArrow.map(m);
					refP = m.map(refP);
					QPainterPath pa2 = EndArrow.toQPainterPath(true);
					QRectF br2 = pa2.boundingRect();
					QTransform m2;
					FPoint grOffset2(getMinClipF(&EndArrow));
					m2.translate(-grOffset2.x(), -grOffset2.y());
					EndArrow.map(m2);
					refP = m2.map(refP);
					EndArrow.translate(-refP.x(), -refP.y());
					QTransform arrowTrans;
					arrowTrans.translate(-m_Doc->currentPage()->xOffset(), -m_Doc->currentPage()->yOffset());
					arrowTrans.translate(Start.x() + ite->xPos(), Start.y() + ite->yPos());
					EndArrow.map(arrowTrans);
					int zS = m_Doc->itemAdd(PageItem::Polygon, PageItem::Unspecified, baseX, baseY, 10, 10, 0, CurrColorStroke, CommonStrings::None, true);
					PageItem *iteS = m_Doc->Items->at(zS);
					iteS->PoLine = EndArrow.copy();
					finishItem(iteS);
					break;
				}
			}
		}
	}
}

void RawVsdPainter::finishItem(PageItem* ite)
{
	ite->ClipEdited = true;
	ite->FrameType = 3;
	ite->setFillShade(CurrFillShade);
	ite->setFillEvenOdd(fillrule);
	ite->setLineShade(CurrStrokeShade);
	ite->setLineJoin(lineJoin);
	ite->setLineEnd(lineEnd);
	if (dashArray.count() > 0)
	{
		ite->DashValues = dashArray;
	}
	FPoint wh = getMaxClipF(&ite->PoLine);
	ite->setWidthHeight(wh.x(),wh.y(), true);
	ite->setTextFlowMode(PageItem::TextFlowUsesBoundingBox);
	m_Doc->AdjustItemSize(ite);
	ite->OldB2 = ite->width();
	ite->OldH2 = ite->height();
	ite->setFillTransparency(CurrFillTrans);
	ite->setLineTransparency(CurrStrokeTrans);
	ite->updateClip();
	Elements->append(ite);
	if (groupStack.count() != 0)
		groupStack.top().Items.append(ite);
	Coords.resize(0);
	Coords.svgInit();
}

VsdPlug::VsdPlug(ScribusDoc* doc, int flags)
{
	tmpSel=new Selection(this, false);
	m_Doc=doc;
	importerFlags = flags;
	interactive = (flags & LoadSavePlugin::lfInteractive);
	progressDialog = NULL;
}

QImage VsdPlug::readThumbnail(QString fName)
{
	QFileInfo fi = QFileInfo(fName);
	double b, h;
	b = PrefsManager::instance()->appPrefs.docSetupPrefs.pageWidth;
	h = PrefsManager::instance()->appPrefs.docSetupPrefs.pageHeight;
	docWidth = b;
	docHeight = h;
	progressDialog = NULL;
	m_Doc = new ScribusDoc();
	m_Doc->setup(0, 1, 1, 1, 1, "Custom", "Custom");
	m_Doc->setPage(docWidth, docHeight, 0, 0, 0, 0, 0, 0, false, false);
	m_Doc->addPage(0);
	m_Doc->setGUI(false, ScCore->primaryMainWindow(), 0);
	baseX = m_Doc->currentPage()->xOffset();
	baseY = m_Doc->currentPage()->yOffset();
	Elements.clear();
	m_Doc->setLoading(true);
	m_Doc->DoDrawing = false;
	m_Doc->scMW()->setScriptRunning(true);
	QString CurDirP = QDir::currentPath();
	QDir::setCurrent(fi.path());
	if (convert(fName))
	{
		tmpSel->clear();
		QDir::setCurrent(CurDirP);
		if (Elements.count() > 1)
			m_Doc->groupObjectsList(Elements);
		m_Doc->DoDrawing = true;
		m_Doc->m_Selection->delaySignalsOn();
		QImage tmpImage;
		if (Elements.count() > 0)
		{
			for (int dre=0; dre<Elements.count(); ++dre)
			{
				tmpSel->addItem(Elements.at(dre), true);
			}
			tmpSel->setGroupRect();
			double xs = tmpSel->width();
			double ys = tmpSel->height();
			tmpImage = Elements.at(0)->DrawObj_toImage(500);
			tmpImage.setText("XSize", QString("%1").arg(xs));
			tmpImage.setText("YSize", QString("%1").arg(ys));
		}
		m_Doc->scMW()->setScriptRunning(false);
		m_Doc->setLoading(false);
		m_Doc->m_Selection->delaySignalsOff();
		delete m_Doc;
		return tmpImage;
	}
	else
	{
		QDir::setCurrent(CurDirP);
		m_Doc->DoDrawing = true;
		m_Doc->scMW()->setScriptRunning(false);
		delete m_Doc;
	}
	return QImage();
}

bool VsdPlug::import(QString fNameIn, const TransactionSettings& trSettings, int flags, bool showProgress)
{
	QString fName = fNameIn;
	bool success = false;
	interactive = (flags & LoadSavePlugin::lfInteractive);
	importerFlags = flags;
	cancel = false;
	double b, h;
	bool ret = false;
	QFileInfo fi = QFileInfo(fName);
	if ( !ScCore->usingGUI() )
	{
		interactive = false;
		showProgress = false;
	}
	if ( showProgress )
	{
		ScribusMainWindow* mw=(m_Doc==0) ? ScCore->primaryMainWindow() : m_Doc->scMW();
		progressDialog = new MultiProgressDialog( tr("Importing: %1").arg(fi.fileName()), CommonStrings::tr_Cancel, mw );
		QStringList barNames, barTexts;
		barNames << "GI";
		barTexts << tr("Analyzing File:");
		QList<bool> barsNumeric;
		barsNumeric << false;
		progressDialog->addExtraProgressBars(barNames, barTexts, barsNumeric);
		progressDialog->setOverallTotalSteps(3);
		progressDialog->setOverallProgress(0);
		progressDialog->setProgress("GI", 0);
		progressDialog->show();
		connect(progressDialog, SIGNAL(canceled()), this, SLOT(cancelRequested()));
		qApp->processEvents();
	}
	else
		progressDialog = NULL;
/* Set default Page to size defined in Preferences */
	b = 0.0;
	h = 0.0;
	if (progressDialog)
	{
		progressDialog->setOverallProgress(1);
		qApp->processEvents();
	}
	if (b == 0.0)
		b = PrefsManager::instance()->appPrefs.docSetupPrefs.pageWidth;
	if (h == 0.0)
		h = PrefsManager::instance()->appPrefs.docSetupPrefs.pageHeight;
	docWidth = b;
	docHeight = h;
	baseX = 0;
	baseY = 0;
	if (!interactive || (flags & LoadSavePlugin::lfInsertPage))
	{
		m_Doc->setPage(docWidth, docHeight, 0, 0, 0, 0, 0, 0, false, false);
		m_Doc->addPage(0);
		m_Doc->view()->addPage(0, true);
		baseX = 0;
		baseY = 0;
	}
	else
	{
		if (!m_Doc || (flags & LoadSavePlugin::lfCreateDoc))
		{
			m_Doc=ScCore->primaryMainWindow()->doFileNew(docWidth, docHeight, 0, 0, 0, 0, 0, 0, false, false, 0, false, 0, 1, "Custom", true);
			ScCore->primaryMainWindow()->HaveNewDoc();
			ret = true;
			baseX = 0;
			baseY = 0;
			baseX = m_Doc->currentPage()->xOffset();
			baseY = m_Doc->currentPage()->yOffset();
		}
	}
	if ((!ret) && (interactive))
	{
		baseX = m_Doc->currentPage()->xOffset();
		baseY = m_Doc->currentPage()->yOffset();
	}
	if ((ret) || (!interactive))
	{
		if (docWidth > docHeight)
			m_Doc->setPageOrientation(1);
		else
			m_Doc->setPageOrientation(0);
		m_Doc->setPageSize("Custom");
	}
	if (!(flags & LoadSavePlugin::lfLoadAsPattern))
		m_Doc->view()->Deselect();
	Elements.clear();
	m_Doc->setLoading(true);
	m_Doc->DoDrawing = false;
	if (!(flags & LoadSavePlugin::lfLoadAsPattern))
		m_Doc->view()->updatesOn(false);
	m_Doc->scMW()->setScriptRunning(true);
	qApp->setOverrideCursor(QCursor(Qt::WaitCursor));
	QString CurDirP = QDir::currentPath();
	QDir::setCurrent(fi.path());
	if (convert(fName))
	{
		tmpSel->clear();
		QDir::setCurrent(CurDirP);
		if ((Elements.count() > 1) && (!(importerFlags & LoadSavePlugin::lfCreateDoc)))
			m_Doc->groupObjectsList(Elements);
		m_Doc->DoDrawing = true;
		m_Doc->scMW()->setScriptRunning(false);
		m_Doc->setLoading(false);
		qApp->changeOverrideCursor(QCursor(Qt::ArrowCursor));
		if ((Elements.count() > 0) && (!ret) && (interactive))
		{
			if (flags & LoadSavePlugin::lfScripted)
			{
				bool loadF = m_Doc->isLoading();
				m_Doc->setLoading(false);
				m_Doc->changed();
				m_Doc->setLoading(loadF);
				if (!(flags & LoadSavePlugin::lfLoadAsPattern))
				{
					m_Doc->m_Selection->delaySignalsOn();
					for (int dre=0; dre<Elements.count(); ++dre)
					{
						m_Doc->m_Selection->addItem(Elements.at(dre), true);
					}
					m_Doc->m_Selection->delaySignalsOff();
					m_Doc->m_Selection->setGroupRect();
					m_Doc->view()->updatesOn(true);
				}
			}
			else
			{
				m_Doc->DragP = true;
				m_Doc->DraggedElem = 0;
				m_Doc->DragElements.clear();
				m_Doc->m_Selection->delaySignalsOn();
				for (int dre=0; dre<Elements.count(); ++dre)
				{
					tmpSel->addItem(Elements.at(dre), true);
				}
				tmpSel->setGroupRect();
				ScElemMimeData* md = ScriXmlDoc::WriteToMimeData(m_Doc, tmpSel);
				m_Doc->itemSelection_DeleteItem(tmpSel);
				m_Doc->view()->updatesOn(true);
				if (importedPatterns.count() != 0)
				{
					for (int cd = 0; cd < importedPatterns.count(); cd++)
					{
						m_Doc->docPatterns.remove(importedPatterns[cd]);
					}
				}
				if (importedColors.count() != 0)
				{
					for (int cd = 0; cd < importedColors.count(); cd++)
					{
						m_Doc->PageColors.remove(importedColors[cd]);
					}
				}
				m_Doc->m_Selection->delaySignalsOff();
				// We must copy the TransationSettings object as it is owned
				// by handleObjectImport method afterwards
				TransactionSettings* transacSettings = new TransactionSettings(trSettings);
				m_Doc->view()->handleObjectImport(md, transacSettings);
				m_Doc->DragP = false;
				m_Doc->DraggedElem = 0;
				m_Doc->DragElements.clear();
			}
		}
		else
		{
			m_Doc->changed();
			m_Doc->reformPages();
			if (!(flags & LoadSavePlugin::lfLoadAsPattern))
				m_Doc->view()->updatesOn(true);
		}
		success = true;
	}
	else
	{
		QDir::setCurrent(CurDirP);
		m_Doc->DoDrawing = true;
		m_Doc->scMW()->setScriptRunning(false);
		m_Doc->view()->updatesOn(true);
		qApp->changeOverrideCursor(QCursor(Qt::ArrowCursor));
	}
	if (interactive)
		m_Doc->setLoading(false);
	//CB If we have a gui we must refresh it if we have used the progressbar
	if (!(flags & LoadSavePlugin::lfLoadAsPattern))
	{
		if ((showProgress) && (!interactive))
			m_Doc->view()->DrawNew();
	}
	qApp->restoreOverrideCursor();
	return success;
}

VsdPlug::~VsdPlug()
{
	if (progressDialog)
		delete progressDialog;
	delete tmpSel;
}

bool VsdPlug::convert(QString fn)
{
	QString tmp;
	importedColors.clear();
	importedPatterns.clear();
	QFile file(fn);
	if ( !file.exists() )
	{
		qDebug() << "File " << QFile::encodeName(fn).data() << " does not exist" << endl;
		return false;
	}
	QFileInfo fi = QFileInfo(fn);
	QString ext = fi.suffix().toLower();
	RawVsdPainter painter;
	painter.m_Doc = m_Doc;
	painter.baseX = baseX;
	painter.baseY = baseY;
	painter.docWidth = docWidth;
	painter.docHeight = docHeight;
	painter.importerFlags = importerFlags;
	painter.Elements = &Elements;
	painter.importedColors = &importedColors;
	painter.importedPatterns = &importedPatterns;
	painter.tmpSel = tmpSel;
	WPXFileStream input(QFile::encodeName(fn).data());
	if (!libvisio::VisioDocument::isSupported(&input))
	{
		qDebug() << "ERROR: Unsupported file format!";
		return false;
	}
	if (!libvisio::VisioDocument::parse(&input, &painter))
	{
		qDebug() << "ERROR: Parsing failed!";
		return false;
	}
	if (Elements.count() == 0)
	{
		if (importedColors.count() != 0)
		{
			for (int cd = 0; cd < importedColors.count(); cd++)
			{
				m_Doc->PageColors.remove(importedColors[cd]);
			}
		}
		if (importedPatterns.count() != 0)
		{
			for (int cd = 0; cd < importedPatterns.count(); cd++)
			{
				m_Doc->docPatterns.remove(importedPatterns[cd]);
			}
		}
	}
	if (progressDialog)
		progressDialog->close();
	return true;
}
