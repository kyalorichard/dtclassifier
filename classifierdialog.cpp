/***************************************************************************
  classifierdialog.cpp
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

#include <QComboBox>
#include <QDebug>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QColor>
#include <QApplication>
#include <QList>
#include <QDir>

#include <cmath>

#include "gdal.h"
#include "gdal_priv.h"
#include "cpl_conv.h"

#include "opencv2/core/core_c.h"
#include "opencv2/ml/ml.hpp"

#include "qgscontexthelp.h"
#include "qgsgeometry.h"
#include "qgsmaplayerregistry.h"
#include "qgspoint.h"
#include "qgsrasterlayer.h"
#include "qgsvectordataprovider.h"
#include "qgsvectorlayer.h"
#include "qgsvectorfilewriter.h"

#include "classifierdialog.h"
#include "classifierutils.h"

ClassifierDialog::ClassifierDialog( QWidget* parent, QgisInterface* iface )
    : QDialog( parent ),
    mIface( iface )
{
  setupUi( this );

  manageGui();

  // need this for working with rasters
  GDALAllRegister();

  connect( btnOutputFile, SIGNAL( clicked() ), this, SLOT( selectOutputFile() ) );
  connect( rastersList, SIGNAL( itemSelectionChanged() ), this, SLOT( updateInputRasters() ) );
  connect( rbDecisionTree, SIGNAL( toggled( bool ) ), this, SLOT( toggleCheckBoxState( bool ) ) );

  // use Ok button for starting classification
  disconnect( buttonBox, SIGNAL( accepted() ), this, SLOT( accept() ) );
  connect( buttonBox, SIGNAL( accepted() ), this, SLOT( doClassification() ) );
}

ClassifierDialog::~ClassifierDialog()
{
}

void ClassifierDialog::selectOutputFile()
{
  // get last used directory
  QSettings settings( "NextGIS", "DTclassifier" );
  QString lastDir;

  lastDir = settings.value( "lastUsedDir", "." ).toString();

  QString fileName = QFileDialog::getSaveFileName( this, tr( "Select output file" ), lastDir, "GeoTiff (*.tif *.tiff *.TIF *.TIFF)" );

  if ( fileName.isEmpty() )
  {
    return;
  }

  // ensure the user never ommited the extension from the file name
  if ( !fileName.toLower().endsWith( ".tif" ) && !fileName.toLower().endsWith( ".tiff" ) )
  {
    fileName += ".tif";
  }

  mOutputFileName = fileName;
  leOutputRaster->setText( mOutputFileName );

  // save last used directory
  settings.setValue( "lastUsedDir", QFileInfo( fileName ).absolutePath() );

  enableOrDisableOkButton();
  qDebug() << "OutFileName" << mOutputFileName;
}

void ClassifierDialog::doClassification()
{
  // save checkboxes state
  QSettings settings( "NextGIS", "DTclassifier" );

  settings.setValue( "discreteClasses", discreteLabelsCheckBox->isChecked() );
  settings.setValue( "addToCanvas", addToCanvasCheckBox->isChecked() );
  settings.setValue( "saveTempLayers", savePointLayersCheckBox->isChecked() );

  if ( mInputRasters.count() == 1 )
  {
    QString inputRaster = rasterLayerByName( mInputRasters.at( 0 ) )->source();
    rasterClassification( inputRaster );
  }
  else
  {
    rasterClassification( createSingleBandRaster() );
    removeDirectory( QDir().tempPath() + "/dtclassifier" );
  }

  // add classified raster to map canvas if requested
  if ( addToCanvasCheckBox->isChecked() )
  {
    QgsRasterLayer* newLayer;
    newLayer = new QgsRasterLayer( mOutputFileName, QFileInfo( mOutputFileName ).baseName() );
    applyRasterStyle( newLayer );
    QgsMapLayerRegistry::instance()->addMapLayer( newLayer );
  }
}

//~ void ClassifierDialog::on_buttonBox_accepted()
//~ {
  //~ accept();
//~ }

void ClassifierDialog::on_buttonBox_rejected()
{
  reject();
}

//~ void ClassifierDialog::on_buttonBox_helpRequested()
//~ {
  //~ QgsContextHelp::run( context_id );
//~ }

QgsVectorLayer* ClassifierDialog::pointsFromPolygons( QgsVectorLayer* polygonLayer, double* geoTransform, const QString& layerName )
{
  QgsVectorLayer* pointsLayer = new QgsVectorLayer( "Point", layerName, "memory" );
  QgsVectorDataProvider *memoryProvider = pointsLayer->dataProvider();
  QgsVectorDataProvider *provider = polygonLayer->dataProvider();

  // TODO: add attributes to provider

  QgsFeature feat;
  QgsGeometry* geom;
  QgsRectangle bbox;
  double xMin, xMax, yMin, yMax;
  double startCol, startRow, endCol, endRow;
  double x, y;
  QgsPoint* pnt = new QgsPoint();
  QgsFeature* ft;
  QgsFeatureList lstFeatures;

  provider->rewind();
  provider->select();

  progressBar->setRange( 0, provider->featureCount() );
  progressBar->setValue( 0 );
  progressBar->setFormat( "Generate points: %p%" );

  while ( provider->nextFeature( feat ) )
  {
    geom = feat.geometry();
    bbox = geom->boundingBox();

    xMin = bbox.xMinimum();
    xMax = bbox.xMaximum();
    yMin = bbox.yMinimum();
    yMax = bbox.yMaximum();

    mapToPixel( xMin, yMax, geoTransform, startRow, startCol);
    mapToPixel( xMax, yMin, geoTransform, endRow, endCol);

    for ( int row = startRow; row < endRow + 1; row++ )
    {
      for ( int col = startCol; col < endCol + 1; col++ )
      {
        // create point and test
        pixelToMap( row - 0.5, col - 0.5, geoTransform, x, y );
        pnt->setX( x );
        pnt->setY( y );
        if ( geom->contains( pnt ) )
        {
          ft = new QgsFeature();
          ft->setGeometry( QgsGeometry::fromPoint( *pnt ) );
          lstFeatures.append( *ft );
        }
      }
    }
    // update progress and process messages
    progressBar->setValue( progressBar->value() + 1 );
    QApplication::processEvents();
  }
  // write to memory layer
  memoryProvider->addFeatures( lstFeatures );
  pointsLayer->updateExtents();

  progressBar->setFormat( "%p%" );
  progressBar->setRange( 0, 100 );
  progressBar->setValue( 0 );

  return pointsLayer;
}

void ClassifierDialog::rasterClassification( const QString& rasterFileName )
{
  QgsVectorLayer *polygonPresence = vectorLayerByName( cmbPresenceLayer->currentText() );
  QgsVectorLayer *polygonAbsence = vectorLayerByName( cmbAbsenceLayer->currentText() );

  // read input raster metadata. We need them to create output raster
  GDALDataset *inRaster;
  inRaster = (GDALDataset *) GDALOpen( rasterFileName.toUtf8(), GA_ReadOnly );
  qDebug() << "input raster opened";

  double geotransform[6];
  int xSize, ySize, bandCount;
  xSize = inRaster->GetRasterXSize();
  ySize = inRaster->GetRasterYSize();
  bandCount = inRaster->GetRasterCount();
  inRaster->GetGeoTransform( geotransform );

  // create points from polygons
  QgsVectorLayer *pointsPresence = pointsFromPolygons( polygonPresence, geotransform, "pointsPresence" );
  QgsVectorLayer *pointsAbsence = pointsFromPolygons( polygonAbsence, geotransform, "pointsAbsence" );

  long featCount = pointsAbsence->featureCount() + pointsPresence->featureCount();
  qDebug() << "Feature count" << featCount;

  // save temporary layers on disk if requested
  if ( savePointLayersCheckBox->isChecked() )
  {
    QFileInfo fi( polygonPresence->source() );
    QString vectorFilename;
    vectorFilename = fi.absoluteDir().absolutePath() + "/" + fi.baseName() + "_points.shp";

    QgsCoordinateReferenceSystem destCRS;
    destCRS = pointsPresence->crs();

    QgsVectorFileWriter::WriterError error;
    QString errorMessage;
    error = QgsVectorFileWriter::writeAsVectorFormat(
              pointsPresence, vectorFilename, "System", &destCRS,
              "ESRI Shapefile",
              false,
              &errorMessage );

    if ( error != QgsVectorFileWriter::NoError )
    {
      QMessageBox::warning( this, "Save error", tr( "Export to vector file failed.\nError: %1" ).arg( errorMessage ) );
    }

    // -- absence layer
    fi.setFile( polygonAbsence->source() );
    vectorFilename = fi.absoluteDir().absolutePath() + "/" + fi.baseName() + "_points.shp";
    destCRS = pointsAbsence->crs();

    error = QgsVectorFileWriter::writeAsVectorFormat(
              pointsAbsence, vectorFilename, "System", &destCRS,
              "ESRI Shapefile",
              false,
              &errorMessage );

    if ( error != QgsVectorFileWriter::NoError )
    {
      QMessageBox::warning( this, "Save error", tr( "Export to vector file failed.\nError: %1" ).arg( errorMessage ) );
    }
  }

  // create output file
  GDALDriver *driver;
  driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  GDALDataset *outRaster;
  outRaster = driver->Create( mOutputFileName.toUtf8(), xSize, ySize, 1, GDT_Float32, NULL );
  outRaster->SetGeoTransform( geotransform );
  outRaster->SetProjection( inRaster->GetProjectionRef() );
  qDebug() << "output raster created";

  // collect train data
  double mapX, mapY;
  double pixX, pixY;
  int row, col;
  int i = 0;

  QgsFeature feat;
  QgsPoint pnt;

  QVector<float> rasterData( xSize * bandCount );

  CvMat* data = cvCreateMat( featCount, bandCount, CV_32F );
  CvMat* responses = cvCreateMat( featCount, 1, CV_32F );

  QgsVectorDataProvider *provider = pointsPresence->dataProvider();
  provider->rewind();
  provider->select();
  
  progressBar->setValue( 0 );
  progressBar->setFormat( "Collect train data: %p%" );
  progressBar->setRange( 0, provider->featureCount() );

  while ( provider->nextFeature( feat ) )
  {
    pnt = feat.geometry()->asPoint();
    mapX = pnt.x();
    mapY = pnt.y();
    mapToPixel( mapX, mapY, geotransform, pixX, pixY);
    col = floor( pixX );
    row = floor( pixY );
    inRaster->RasterIO( GF_Read, col , row, 1, 1, (void*)rasterData.data(), 1, 1, GDT_Float32, bandCount, 0, 0, 0 , 0 );
    for (int j = 0; j < bandCount; j++)
    {
      cvmSet( data, i, j, rasterData[ j ] );
    }
    //~ cvmSet( data, i, 0, rasterData[0] );
    //~ cvmSet( data, i, 1, rasterData[1] );
    //~ cvmSet( data, i, 2, rasterData[2] );
    //~ cvmSet( data, i, 3, rasterData[3] );
    //~ cvmSet( data, i, 4, rasterData[4] );
    //~ cvmSet( data, i, 5, rasterData[5] );
    cvmSet( responses, i, 0, 1 );
    i++;
    
    progressBar->setValue( progressBar->value() + 1 );
    QApplication::processEvents();
  }

  provider = pointsAbsence->dataProvider();
  provider->rewind();
  provider->select();
  
  progressBar->setRange( 0, provider->featureCount() );

  while ( provider->nextFeature( feat ) )
  {
    pnt = feat.geometry()->asPoint();
    mapX = pnt.x();
    mapY = pnt.y();
    mapToPixel( mapX, mapY, geotransform, pixX, pixY);
    col = floor( pixX );
    row = floor( pixY );
    inRaster->RasterIO( GF_Read, col , row, 1, 1, (void*)rasterData.data(), 1, 1, GDT_Float32, bandCount, 0, 0, 0 , 0 );
    for (int j = 0; j < bandCount; j++)
    {
      cvmSet( data, i, j, rasterData[ j ] );
    }
    //~ cvmSet( data, i, 0, rasterData[0] );
    //~ cvmSet( data, i, 1, rasterData[1] );
    //~ cvmSet( data, i, 2, rasterData[2] );
    //~ cvmSet( data, i, 3, rasterData[3] );
    //~ cvmSet( data, i, 4, rasterData[4] );
    //~ cvmSet( data, i, 5, rasterData[5] );
    cvmSet( responses, i, 0, 0 );
    i++;

    progressBar->setValue( progressBar->value() + 1 );
    QApplication::processEvents();
  }
  
  progressBar->setValue( 0 );
  progressBar->setFormat( "%p%" );
  progressBar->setRange( 0, 100 );

  CvDTree* dtree = new CvDTree();
  CvRTrees* rtree = new CvRTrees();
  // use decision tree
  if ( rbDecisionTree->isChecked() )
  {
    // build decision tree classifier
    if ( discreteLabelsCheckBox->isChecked() )
    {
      CvMat* var_type;
      var_type = cvCreateMat( data->cols + 1, 1, CV_8U );
      cvSet( var_type, cvScalarAll(CV_VAR_CATEGORICAL) );
      dtree->train( data, CV_ROW_SAMPLE, responses, 0, 0, var_type );
      cvReleaseMat( &var_type );
    }
    else
    {
      dtree->train( data, CV_ROW_SAMPLE, responses, 0, 0 );
    }

    QFileInfo fi( mOutputFileName );
    QString treeFileName;
    treeFileName = fi.absoluteDir().absolutePath() + "/" + fi.baseName() + "_tree.yaml";

    dtree->save( treeFileName.toUtf8(), "MyTree" );
  }
  else // or random trees
  {
    // build random trees classifier
    rtree->train( data, CV_ROW_SAMPLE, responses );
  }

  cvReleaseMat( &data );
  cvReleaseMat( &responses );

  // classify raster using tree
  CvMat* sample = cvCreateMat( bandCount, 1, CV_32F );
  QVector<float> outData( xSize );

  progressBar->setFormat( "Classification: %p%" );
  progressBar->setRange( 0, ySize );
  progressBar->setValue( 0 );

  for ( int row = 0; row < ySize; ++row )
  {
    inRaster->RasterIO( GF_Read, 0, row, xSize, 1, (void *)rasterData.data(), xSize, 1, GDT_Float32, bandCount, 0, 0, 0 , 0 );
    for ( int col = 0; col < xSize; ++col )
    {
      for (int j = 0; j < bandCount; j++)
      {
        cvmSet( sample, j, 0, rasterData[ xSize * j + col ] );
      }
      //~ cvmSet( sample, 0, 0, rasterData[ col ] );
      //~ cvmSet( sample, 1, 0, rasterData[ xSize + col ] );
      //~ cvmSet( sample, 2, 0, rasterData[ xSize*2 + col ] );
      //~ cvmSet( sample, 3, 0, rasterData[ xSize*3 + col ] );
      //~ cvmSet( sample, 4, 0, rasterData[ xSize*4 + col ] );
      //~ cvmSet( sample, 5, 0, rasterData[ xSize*5 + col ] );

      if ( rbDecisionTree->isChecked() )
      {
        outData[ col ] = dtree->predict( sample )->value;
      }
      else
      {
        outData[ col ] = rtree->predict( sample );
      }
    }
    outRaster->RasterIO( GF_Write, 0, row, xSize, 1, (void *)outData.data(), xSize, 1, GDT_Float32, 1, 0, 0, 0 , 0 );
    progressBar->setValue( progressBar->value() + 1 );
    QApplication::processEvents();
  }

  // cleanup
  progressBar->setFormat( "%p%" );
  progressBar->setRange( 0, 100 );
  progressBar->setValue( 0 );
  cvReleaseMat( &sample );
  dtree->clear();
  delete dtree;
  rtree->clear();
  delete rtree;

  GDALClose( (GDALDatasetH) inRaster );
  GDALClose( (GDALDatasetH) outRaster );
}

void ClassifierDialog::manageGui()
{
  // restore ui state from settings
  QSettings settings( "NextGIS", "DTclassifier" );

  addToCanvasCheckBox->setChecked( settings.value( "addToCanvas", false ).toBool() );
  savePointLayersCheckBox->setChecked( settings.value( "saveTempLayers", false ).toBool() );

  // classification settings
  QString algorithm = settings.value( "classificationAlg", "dtree" ).toString();
  if ( algorithm == "dtree" )
  {
    rbDecisionTree->setChecked( true );
    discreteLabelsCheckBox->setEnabled( true );
  }
  else
  {
    rbRandomTrees->setChecked( true );
    discreteLabelsCheckBox->setEnabled( false );
  }
  discreteLabelsCheckBox->setChecked( settings.value( "discreteClasses", true ).toBool() );

  // populate vector layers comboboxes
  QMap<QString, QgsMapLayer*> mapLayers = QgsMapLayerRegistry::instance()->mapLayers();
  QMap<QString, QgsMapLayer*>::iterator layer_it = mapLayers.begin();
  
  QgsRasterLayer* layer;

  for ( ; layer_it != mapLayers.end(); ++layer_it )
  {
    if ( layer_it.value()->type() == QgsMapLayer::VectorLayer )
    {
      if ( qobject_cast<QgsVectorLayer *>( layer_it.value() )->geometryType() == QGis::Polygon )
      {
        cmbPresenceLayer->addItem( layer_it.value()->name() );
        cmbAbsenceLayer->addItem( layer_it.value()->name() );
      }
    }
    else if ( layer_it.value()->type() == QgsMapLayer::RasterLayer )
    {
      layer = qobject_cast<QgsRasterLayer *> ( layer_it.value() );
      if ( layer->usesProvider() && layer->providerKey() != "gdal" )
      {
        continue;
      }
      rastersList->addItem( new QListWidgetItem( layer_it.value()->name() ) );
    }
  }
}

void ClassifierDialog::toggleCheckBoxState( bool checked )
{
  if ( checked )
  {
    discreteLabelsCheckBox->setEnabled( true );
  }
  else
  {
    discreteLabelsCheckBox->setEnabled( false );
  }
}

void ClassifierDialog::updateInputRasters()
{
  QList<QListWidgetItem *> selection = rastersList->selectedItems();
  
  mInputRasters.clear();
  
  for ( int i = 0; i < selection.size(); ++i )
  {
    // write file path instead of layer name ?
    mInputRasters.append( selection.at( i )->text() );
  }
}

void ClassifierDialog::enableOrDisableOkButton()
{
  bool enabled = true;

  if ( mOutputFileName.isEmpty() )
  {
    enabled = false;
  }

  buttonBox->button( QDialogButtonBox::Ok )->setEnabled( enabled );
}

void ClassifierDialog::applyRasterStyle( QgsRasterLayer* layer )
{
  // draw as singleBand image with ColorRampShader
  layer->setDrawingStyle( QgsRasterLayer::SingleBandPseudoColor );
  layer->setColorShadingAlgorithm( QgsRasterLayer::ColorRampShader );
  
  // create color ramp
  QgsColorRampShader* myRasterShaderFunction = ( QgsColorRampShader* )layer->rasterShader()->rasterShaderFunction();
  QList<QgsColorRampShader::ColorRampItem> myColorRampItems;

  QgsColorRampShader::ColorRampItem absenceItem, presenceItem;
  absenceItem.value = 0;
  absenceItem.color = QColor( Qt::white );
  absenceItem.label = "";

  presenceItem.value = 1;
  presenceItem.color = QColor( Qt::red );
  presenceItem.label = "";
  myColorRampItems.append( absenceItem );
  myColorRampItems.append( presenceItem );

  // sort the shader items
  qSort( myColorRampItems );
  myRasterShaderFunction->setColorRampItemList( myColorRampItems );

  // make 0 transparent
  layer->rasterTransparency()->initializeTransparentPixelList( 0.0 );
}

QString ClassifierDialog::createSingleBandRaster()
{
  QString layerName, layerPath;
  GDALDataset* raster;
  int bandCount, rasterCount;
  
  rasterCount = 1;
  
  // create dir for our temp files
  QString tempDir = QDir().tempPath() + "/dtclassifier";
  if ( !QDir().mkpath( tempDir ) )
  {
    qDebug() << "Can't create temporary directory" << tempDir;
  }
  
  QString templateName = tempDir + "/raster_";
  QString rasterName;
  
  QProcess* process = new QProcess();
  QString command = "gdal_translate";
  QStringList args;
  
  //connect( process, SIGNAL( finished( int, QProcess::ExitStatus ) ), this, SLOT( processFinished( int, QProcess::ExitStatus ) ) );
  connect( process, SIGNAL( finished( int, QProcess::ExitStatus ) ), this, SLOT( updateProgressBar() ) );

  // iterate over rasters
  for (int i = 0; i < mInputRasters.size(); ++i )
  {
    layerName = mInputRasters.at( i );
    layerPath = rasterLayerByName( layerName )->source();

    raster = (GDALDataset*) GDALOpen( layerPath.toUtf8(), GA_ReadOnly );
    bandCount = raster->GetRasterCount();
    GDALClose( raster );
    
    progressBar->setValue( 0 );
    progressBar->setFormat( layerName + ": %p%" );
    progressBar->setRange( 0, bandCount );
   
    // iterate over bands
    for ( int j = 0; j < bandCount; ++j )
    {
      rasterName = templateName + QString( "%1.tif" ).arg( rasterCount );
      
      args.clear();
      args << "-b" << QString::number( j + 1 ) << layerPath << rasterName;
      
      process->start( command, args, QIODevice::ReadOnly );
      if ( !process->waitForFinished( -1 ) )
      {
        qDebug() << "Failed to extract bands from raster" << layerPath;
        return QString();
      }
      rasterCount++;
    } //for j
  } //for i

  disconnect( process, SIGNAL( finished( int, QProcess::ExitStatus ) ), this, SLOT( updateProgressBar() ) );
  connect( process, SIGNAL( readyReadStandardOutput() ), this, SLOT( updateProgressBar() ) );
  
  progressBar->setValue( 0 );
  progressBar->setFormat( "Merge bands: %p%" );
  progressBar->setRange( 0, 20 );

  // get raster files in temp dir
  QDir workDir( tempDir );
  workDir.setFilter( QDir::Files | QDir::NoSymLinks | QDir::NoDotAndDotDot );
  QStringList nameFilter( "*.tif" );
  workDir.setNameFilters( nameFilter );
  QStringList bands = workDir.entryList();
  
  rasterName = tempDir + "/output.img";
  
  // merge into single raster
  #ifdef Q_OS_WIN32
    command = "gdal_merge.bat";
  #else
    command = "gdal_merge.py";
  #endif
  args.clear();
  args << "-separate" << "-of" << "HFA" << "-o" << rasterName << bands;
  
  process->setWorkingDirectory( tempDir );
  process->setReadChannel( QProcess::StandardOutput );
  process->start( command, args, QIODevice::ReadOnly );
  qDebug() << "command:" << command << args.join( " " );
  process->waitForFinished( -1 );

  progressBar->setValue( 0 );
  progressBar->setFormat( "%p%" );
  progressBar->setRange( 0, 100 );
  
  return rasterName;
}

//~ void ClassifierDialog::processFinished( int exitCode, QProcess::ExitStatus exitStatus )
//~ {
  //~ progressBar->setValue( progressBar->value() + 1 );
  //~ QApplication::processEvents();
//~ }

void ClassifierDialog::updateProgressBar()
{
  progressBar->setValue( progressBar->value() + 1 );
  QApplication::processEvents();
}

// -------------- Coordinate transform routines ------------------------

void ClassifierDialog::mapToPixel( double mX, double mY, double* geoTransform, double& outX, double& outY )
{
  double oX, oY;
  if ( geoTransform[ 2 ] + geoTransform[ 4 ] == 0 )
  {
    oX = ( mX - geoTransform[ 0 ] ) / geoTransform[ 1 ];
    oY = ( mY - geoTransform[ 3 ] ) / geoTransform[ 5 ];
  }
  else
  {
    double invGeoTransform [ 6 ] = { 0, 0, 0, 0, 0, 0 };
    invertGeoTransform( geoTransform, invGeoTransform );
    applyGeoTransform( mX, mY, invGeoTransform, oX, oY );
  }
  outX = floor( oX + 0.5 );
  outY = floor( oY + 0.5 );
}

void ClassifierDialog::pixelToMap( double pX, double pY, double* geoTransform, double& outX, double& outY )
{
  applyGeoTransform( pX, pY, geoTransform, outX, outY );
}

void ClassifierDialog::applyGeoTransform( double inX, double inY, double* geoTransform, double& outX, double& outY )
{
  outX = geoTransform[ 0 ] + inX * geoTransform[ 1 ] + inY * geoTransform[ 2 ];
  outY = geoTransform[ 3 ] + inX * geoTransform[ 4 ] + inY * geoTransform[ 5 ];
}

void ClassifierDialog::invertGeoTransform( double* inGeoTransform, double* outGeoTransform )
{
  double det = inGeoTransform[ 1 ] * inGeoTransform[ 5 ] - inGeoTransform[ 2 ] * inGeoTransform[ 4 ];

  if ( fabs( det ) < 0.000000000000001 )
  {
    return;
  }

  double invDet = 1.0 / det;

  outGeoTransform[ 1 ] = inGeoTransform[ 5 ] * invDet;
  outGeoTransform[ 4 ] = -inGeoTransform[ 4 ] * invDet;

  outGeoTransform[ 2 ] = -inGeoTransform[ 2 ] * invDet;
  outGeoTransform[ 5 ] = inGeoTransform[ 1 ] * invDet;

  outGeoTransform[ 0 ] = ( inGeoTransform[ 2 ] * inGeoTransform[ 3 ] - inGeoTransform[ 0 ] * inGeoTransform[ 5 ] ) * invDet;
  outGeoTransform[ 3 ] = ( -inGeoTransform[ 1 ] * inGeoTransform[ 3 ] + inGeoTransform[ 0 ] * inGeoTransform[ 4 ] ) * invDet;
}
