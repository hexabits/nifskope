/***** BEGIN LICENSE BLOCK *****

BSD License

Copyright (c) 2005-2015, NIF File Format Library and Tools
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.
3. The name of the NIF File Format Library and Tools project may not be
used to endorse or promote products derived from this software
without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

***** END LICENCE BLOCK *****/

#include "nifskope.h"
#include "ui_nifskope.h"

#include "glview.h"
#include "message.h"
#include "spellbook.h"
#include "gl/glscene.h"
#include "model/kfmmodel.h"
#include "model/nifmodel.h"
#include "model/nifproxymodel.h"
#include "ui/widgets/fileselect.h"
#include "ui/widgets/floatslider.h"
#include "ui/widgets/floatedit.h"
#include "ui/widgets/lightingwidget.h"
#include "ui/widgets/nifview.h"
#include "ui/widgets/refrbrowser.h"
#include "ui/widgets/inspect.h"
#include "ui/widgets/xmlcheck.h"
#include "ui/about_dialog.h"
#include "ui/settingsdialog.h"

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDockWidget>
#include <QFileDialog>
#include <QFontDialog>
#include <QGroupBox>
#include <QHeaderView>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QProgressBar>
#include <QMessageBox>
#include <QSettings>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>

#include <QProcess>
#include <QStyleFactory>
#include <QRegularExpression>

QString nstypes::operator""_uip( const char * str, size_t )
{
	QString u;

#ifndef QT_NO_DEBUG
	u = "UI/Debug/";
#else
	u = "UI/";
#endif

	return u + QString( str );
}

using namespace nstypes;
using namespace nstheme;


QColor NifSkope::defaultsDark[6] = {
	QColor( 60, 60, 60 ),    /// nstheme::Base
	QColor( 50, 50, 50 ),    /// nstheme::BaseAlt
	Qt::white,               /// nstheme::Text
	QColor( 204, 204, 204 ), /// nstheme::Highlight
	Qt::black,               /// nstheme::HighlightText
	QColor( 255, 66, 58 )    /// nstheme::BrightText
};

QColor NifSkope::defaultsLight[6] = {
	QColor( 245, 245, 245 ), /// nstheme::Base
	QColor( 255, 255, 255 ), /// nstheme::BaseAlt
	Qt::black,               /// nstheme::Text
	QColor( 42, 130, 218 ),  /// nstheme::Highlight
	Qt::white,               /// nstheme::HighlightText
	Qt::red                  /// nstheme::BrightText
};



//! @file nifskope_ui.cpp UI logic for %NifSkope's main window.

NifSkope * NifSkope::createWindow( const QString & fname )
{
	NifSkope * skope = new NifSkope;
	skope->setAttribute( Qt::WA_DeleteOnClose );

	// Prevent the main window from opening in the minimized state.
	// This is a fix for this scenario:
	// If you minimize a NifSkope window and then double click on a .nif file in Windows Explorer or any other file manager,
	// the file is opened in a new window that is also minimized and requires an additional click to bring it to the front.
	skope->setWindowState( skope->windowState() & ~Qt::WindowMinimized );

	skope->loadTheme();
	skope->updateFileMenus();
	skope->show();
	skope->restoreUi();
	skope->raise();

	if ( !fname.isEmpty() ) {
		skope->loadFile( fname );
	}

	skope->forceQuickResize();

	return skope;
}

void NifSkope::initActions()
{
	aSanitize = ui->aSanitize;
	aList = ui->aList;
	aHierarchy = ui->aHierarchy;
	aCondition = ui->aCondition;
	aRCondition = ui->aRCondition;

	// Undo/Redo
	undoAction = nif->undoStack->createUndoAction( this, tr( "&Undo" ) );
	undoAction->setShortcut( QKeySequence::Undo );
	undoAction->setObjectName( "aUndo" );
	undoAction->setIcon( QIcon( ":btn/undo" ) );
	redoAction = nif->undoStack->createRedoAction( this, tr( "&Redo" ) );
	redoAction->setShortcut( QKeySequence::Redo );
	redoAction->setObjectName( "aRedo" );
	redoAction->setIcon( QIcon( ":btn/redo" ) );

	// TODO: Back/Forward button in Block List
	//idxForwardAction = indexStack->createRedoAction( this );
	//idxBackAction = indexStack->createUndoAction( this );

	ui->tFile->addAction( undoAction );
	ui->tFile->addAction( redoAction );

	connect( undoAction, &QAction::triggered, [this]( bool ) {
		ogl->update();
	} );

	connect( redoAction, &QAction::triggered, [this]( bool ) {
		ogl->update();
	} );

	ui->aSave->setShortcut( QKeySequence::Save );
	ui->aSaveAs->setShortcut( { "Ctrl+Alt+S" } );
	ui->aWindow->setShortcut( QKeySequence::New );

	connect( ui->aBrowseArchive, &QAction::triggered, this, &NifSkope::archiveDlg );
	connect( ui->aOpen, &QAction::triggered, this, &NifSkope::openDlg );
	connect( ui->aSave, &QAction::triggered, this, &NifSkope::save );  
	connect( ui->aSaveAs, &QAction::triggered, this, &NifSkope::saveAsDlg );

	// TODO: Assure Actions and Scene state are synced
	// Set Data for Actions to pass onto Scene when clicking
	/*	
		ShowAxes = 0x1,
		ShowGrid = 0x2,
		ShowNodes = 0x4,
		ShowCollision = 0x8,
		ShowConstraints = 0x10,
		ShowMarkers = 0x20,
		DoDoubleSided = 0x40, // Not implemented
		DoVertexColors = 0x80,
		DoSpecular = 0x100,
		DoGlow = 0x200,
		DoTexturing = 0x400,
		DoBlending = 0x800,   // Not implemented
		DoMultisampling = 0x1000, // Not implemented
		DoLighting = 0x2000,
		DoCubeMapping = 0x4000,
		DisableShaders = 0x8000,
		ShowHidden = 0x10000
	*/

	ui->aShowAxes->setData( Scene::ShowAxes );
	ui->aShowGrid->setData( Scene::ShowGrid );
	ui->aShowNodes->setData( Scene::ShowNodes );
	ui->aShowCollision->setData( Scene::ShowCollision );
	ui->aShowConstraints->setData( Scene::ShowConstraints );
	ui->aShowMarkers->setData( Scene::ShowMarkers );
	ui->aShowHidden->setData( Scene::ShowHidden );
	ui->aDoSkinning->setData( Scene::DoSkinning );

	ui->aTextures->setData( Scene::DoTexturing );
	ui->aVertexColors->setData( Scene::DoVertexColors );
	ui->aSpecular->setData( Scene::DoSpecular );
	ui->aGlow->setData( Scene::DoGlow );
	ui->aCubeMapping->setData( Scene::DoCubeMapping );
	ui->aLighting->setData( Scene::DoLighting );
	ui->aDisableShading->setData( Scene::DisableShaders );

	ui->aSelectObject->setData( Scene::SelObject );
	ui->aSelectVertex->setData( Scene::SelVertex );

	auto agroup = [this]( QVector<QAction *> actions, bool exclusive ) {
		QActionGroup * ag = new QActionGroup( this );
		for ( auto a : actions ) {
			ag->addAction( a );
		}

		ag->setExclusive( exclusive );

		return ag;
	};

	selectActions = agroup( { ui->aSelectObject, ui->aSelectVertex }, true );
	connect( selectActions, &QActionGroup::triggered, ogl->getScene(), &Scene::updateSelectMode );

	showActions = agroup( { ui->aShowAxes, ui->aShowGrid, ui->aShowNodes, ui->aShowCollision,
						  ui->aShowConstraints, ui->aShowMarkers, ui->aShowHidden, ui->aDoSkinning
	}, false );
	connect( showActions, &QActionGroup::triggered, ogl->getScene(), &Scene::updateSceneOptionsGroup );
	connect( showActions, &QActionGroup::triggered, ogl, &GLView::updateScene );

	shadingActions = agroup( { ui->aTextures, ui->aVertexColors, ui->aSpecular, ui->aGlow, ui->aCubeMapping, ui->aLighting, ui->aDisableShading }, false );
	connect( shadingActions, &QActionGroup::triggered, ogl->getScene(), &Scene::updateSceneOptionsGroup );
	connect( shadingActions, &QActionGroup::triggered, ogl, &GLView::updateScene );

	auto testActions = agroup( { ui->aTest1Dbg, ui->aTest2Dbg, ui->aTest3Dbg }, true );
	connect( testActions, &QActionGroup::triggered, ogl->getScene(), &Scene::updateSceneOptionsGroup );

	// Sync actions to Scene state
	for ( auto a : showActions->actions() ) {
		a->setChecked( ogl->scene->options & a->data().toInt() );
	}

	// Sync actions to Scene state
	for ( auto a : shadingActions->actions() ) {
		a->setChecked( ogl->scene->options & a->data().toInt() );
	}

	// Setup blank QActions for Recent Files menus
	for ( int i = 0; i < NumRecentFiles; ++i ) {
		recentFileActs[i] = new QAction( this );
		recentArchiveActs[i] = new QAction( this );
		recentArchiveFileActs[i] = new QAction( this );

		recentFileActs[i]->setVisible( false );
		recentArchiveActs[i]->setVisible( false );
		recentArchiveFileActs[i]->setVisible( false );

		connect( recentFileActs[i], &QAction::triggered, this, &NifSkope::openRecentFile );
		connect( recentArchiveActs[i], &QAction::triggered, this, &NifSkope::openRecentArchive );
		connect( recentArchiveFileActs[i], &QAction::triggered, this, &NifSkope::openRecentArchiveFile );
	}

	aList->setChecked( list->model() == nif );
	aHierarchy->setChecked( list->model() == proxy );

	// Allow only List or Tree view to be selected at once
	gListMode = new QActionGroup( this );
	gListMode->addAction( aList );
	gListMode->addAction( aHierarchy );
	gListMode->setExclusive( true );
	connect( gListMode, &QActionGroup::triggered, this, &NifSkope::setListMode );

	connect( aCondition, &QAction::toggled, tree, &NifTreeView::setRowHiding );
	connect( aCondition, &QAction::toggled, kfmtree, &NifTreeView::setRowHiding );

	connect( ui->aAboutNifSkope, &QAction::triggered, [this]() {
		auto aboutDialog = new AboutDialog( this );
		aboutDialog->open( true );
	} );
	connect( ui->aAboutQt, &QAction::triggered, qApp, &QApplication::aboutQt );

	connect( ui->aPrintView, &QAction::triggered, ogl, &GLView::saveImage );

#ifdef QT_NO_DEBUG
	ui->aColorKeyDebug->setDisabled( true );
	ui->aColorKeyDebug->setVisible( false );
	ui->aBoundsDebug->setDisabled( true );
	ui->aBoundsDebug->setVisible( false );
#else
	QAction * debugNone = new QAction( this );

	QActionGroup * debugActions = agroup( { debugNone, ui->aColorKeyDebug, ui->aBoundsDebug }, false );
	connect( ui->aColorKeyDebug, &QAction::triggered, [this]( bool checked ) {
		if ( checked )
			ogl->setDebugMode( GLView::DbgColorPicker );
		else
			ogl->setDebugMode( GLView::DbgNone );
		
		ogl->update();
	} );

	connect( ui->aBoundsDebug, &QAction::triggered, [this]( bool checked ) {
		if ( checked )
			ogl->setDebugMode( GLView::DbgBounds );
		else
			ogl->setDebugMode( GLView::DbgNone );

		ogl->update();
	} );

	connect( debugActions, &QActionGroup::triggered, [=]( QAction * action ) {
		for ( auto a : debugActions->actions() ) {
			if ( a == action )
				continue;

			a->setChecked( false );
		}
	} );
#endif

	connect( ui->aSilhouette, &QAction::triggered, [this]( bool checked ) {
		ogl->setVisMode( Scene::VisSilhouette, checked );
		ogl->updateScene();
	} );

	connect( ui->aVisNormals, &QAction::triggered, [this]( bool checked ) {
		ogl->setVisMode( Scene::VisNormalsOnly, checked );
	} );

	connect( ogl, &GLView::clicked, this, &NifSkope::select );
	connect( ogl, &GLView::sceneTimeChanged, inspect, &InspectView::updateTime );
	connect( ogl, &GLView::paintUpdate, inspect, &InspectView::refresh );
	connect( ogl, &GLView::viewModeChanged, this, &NifSkope::updateCurrentViewAction );

	connect( graphicsView, &GLGraphicsView::customContextMenuRequested, this, &NifSkope::contextMenu );

	// Update Inspector widget with current index
	connect( tree, &NifTreeView::sigCurrentIndexChanged, inspect, &InspectView::updateSelection );
}

void NifSkope::initDockWidgets()
{
	dRefr = ui->RefrDock;
	dList = ui->ListDock;
	dTree = ui->TreeDock;
	dHeader = ui->HeaderDock;
	dInsp = ui->InspectDock;
	dKfm = ui->KfmDock;
	dBrowser = ui->BrowserDock;

	// Tabify List and Header
	tabifyDockWidget( dList, dHeader );
	tabifyDockWidget( dHeader, dBrowser );

	// Raise List above Header
	dList->raise();

	// Hide certain docks by default
	dRefr->toggleViewAction()->setChecked( false );
	dInsp->toggleViewAction()->setChecked( false );
	dKfm->toggleViewAction()->setChecked( false );

	dRefr->setVisible( false );
	dInsp->setVisible( false );
	dKfm->setVisible( false );

	ui->menuShow->addAction(dList->toggleViewAction());
	ui->menuShow->addAction(dTree->toggleViewAction());
	ui->menuShow->addAction(dHeader->toggleViewAction());
	ui->menuShow->addAction(dBrowser->toggleViewAction());
	ui->menuShow->addAction(dInsp->toggleViewAction());
	ui->menuShow->addAction(dKfm->toggleViewAction());
	ui->menuShow->addAction(dRefr->toggleViewAction());

	ui->tView->addAction(dList->toggleViewAction());
	ui->tView->addAction(dTree->toggleViewAction());
	ui->tView->addAction(dHeader->toggleViewAction());
	ui->tView->addAction(dBrowser->toggleViewAction());
	ui->tView->addAction(dInsp->toggleViewAction());
	ui->tView->addAction(dKfm->toggleViewAction());
	ui->tView->addAction(dRefr->toggleViewAction());

	ui->mHelp->insertAction( ui->mHelp->actions().at(0), dRefr->toggleViewAction() );

	// Set Inspect widget
	dInsp->setWidget( inspect );

	connect( dList->toggleViewAction(), &QAction::triggered, tree, &NifTreeView::clearRootIndex );

}

void NifSkope::initMenu()
{
	// Populate Toolbars menu with all enabled toolbars
	for ( QObject * o : children() ) {
		QToolBar * tb = qobject_cast<QToolBar *>(o);
		if ( tb && tb->objectName() != "tFile" ) {
			// Do not add tFile to the list
			ui->mToolbars->addAction( tb->toggleViewAction() );
		}
	}

	// Insert SpellBook class before Options
	mSpells = ui->menubar->insertMenu( ui->menubar->actions().at( 3 ), book.get() );

	// Insert Import/Export menus
	mExport = ui->menuExport;
	mImport = ui->menuImport;

	fillImportExportMenus();
	connect( mExport, &QMenu::triggered, this, &NifSkope::sltExport );
	connect( mImport, &QMenu::triggered, this, &NifSkope::sltImport );

	// BSA Recent Files
	mRecentArchiveFiles = new QMenu( this );
	mRecentArchiveFiles->setObjectName( "mRecentArchiveFiles" );

	for ( int i = 0; i < NumRecentFiles; ++i ) {
		ui->mRecentFiles->addAction( recentFileActs[i] );
		ui->mRecentArchives->addAction( recentArchiveActs[i] );
		mRecentArchiveFiles->addAction( recentArchiveFileActs[i] );
	}

	// Load & Save
	QMenu * mSave = new QMenu( this );
	mSave->setObjectName( "mSave" );

	mSave->addAction( ui->aSave );
	mSave->addAction( ui->aSaveAs );

	QMenu * mOpen = new QMenu( this );
	mOpen->setObjectName( "mOpen" );

	mOpen->addAction( ui->aOpen );
	mOpen->addAction( ui->aBrowseArchive );

	aRecentFilesSeparator = mOpen->addSeparator();
	for ( int i = 0; i < NumRecentFiles; ++i )
		mOpen->addAction( recentFileActs[i] );

	auto setFlyout = []( QToolButton * btn, QMenu * m ) {
		btn->setObjectName( "btnFlyoutMenu" );
		btn->setMenu( m );
		btn->setPopupMode( QToolButton::InstantPopup );
	};

	// Append Menu to tFile actions
	for ( auto child : ui->tFile->findChildren<QToolButton *>() ) {
		if ( child->defaultAction() == ui->aSaveMenu ) {
			setFlyout( child, mSave );
		} else if ( child->defaultAction() == ui->aOpenMenu ) {
			setFlyout( child, mOpen );
		}
	}

	updateRecentFileActions();
	updateRecentArchiveActions();
	//updateRecentArchiveFileActions();

	// Lighting Menu
	auto mLight = lightingWidget();

	// Append Menu to tRender actions
	for ( auto child : ui->tRender->findChildren<QToolButton *>() ) {

		if ( child->defaultAction() == ui->aLightMenu ) {
			setFlyout( child, mLight );
		} else {
			child->setObjectName( "btnRender" );
		}
	}


	// BSA Recent Archives
	auto tRecentArchives = new QToolButton( this );
	tRecentArchives->setText( "Recent Archives" );
	setFlyout( tRecentArchives, ui->mRecentArchives );

	// BSA Recent Files
	auto tRecentArchiveFiles = new QToolButton( this );
	tRecentArchiveFiles->setText( "Recent Files" );
	setFlyout( tRecentArchiveFiles, mRecentArchiveFiles );

	ui->bsaTitleBar->layout()->addWidget( tRecentArchives );
	ui->bsaTitleBar->layout()->addWidget( tRecentArchiveFiles );


	// Theme Menu

	QActionGroup * grpTheme = new QActionGroup( this );

	// Fill the action data with the integer correlating to 
	// their position in WindowTheme and add to the action group.
	int i = 0;
	auto themes = ui->mTheme->actions();
	for ( auto a : themes ) {
		a->setData( i++ );
		grpTheme->addAction( a );
	}
}


void NifSkope::initToolBars()
{
	// Add right click menu to tRender toolbar that duplicates all of the toolbar buttons.
	ui->tRender->setContextMenuPolicy( Qt::ActionsContextMenu );

	// Status Bar
	ui->statusbar->setContentsMargins( 0, 0, 0, 0 );
	ui->statusbar->addPermanentWidget( progress );
	
	// TODO: Split off into own widget
	ui->statusbar->addPermanentWidget( filePathWidget( this ) );


	// Render

	viewActions = new QActionGroup( this );
	viewActions->addAction( ui->aViewTop );
	viewActions->addAction( ui->aViewFront );
	viewActions->addAction( ui->aViewLeft );
	viewActions->addAction( ui->aViewWalk );
	viewActions->addAction( ui->aViewUser );
	viewActions->setExclusive( true );

	updateCurrentViewAction();

	// Animate
	connect( ui->aAnimate, &QAction::toggled, ui->tAnim, &QToolBar::setVisible );
	connect( ui->tAnim, &QToolBar::visibilityChanged, ui->aAnimate, &QAction::setChecked );

	/*enum AnimationStates
	{
		AnimDisabled = 0x0,
		AnimEnabled = 0x1,
		AnimPlay = 0x2,
		AnimLoop = 0x4,
		AnimSwitch = 0x8
	};*/

	ui->aAnimate->setData( GLView::AnimEnabled );
	ui->aAnimPlay->setData( GLView::AnimPlay );
	ui->aAnimLoop->setData( GLView::AnimLoop );
	ui->aAnimSwitch->setData( GLView::AnimSwitch );

	connect( ui->aAnimate, &QAction::toggled, ogl, &GLView::updateAnimationState );
	connect( ui->aAnimPlay, &QAction::triggered, ogl, &GLView::updateAnimationState );
	connect( ui->aAnimLoop, &QAction::toggled, ogl, &GLView::updateAnimationState );
	connect( ui->aAnimSwitch, &QAction::toggled, ogl, &GLView::updateAnimationState );

	// Animation timeline slider
	auto animSlider = new FloatSlider( Qt::Horizontal, true, true );
	auto animSliderEdit = new FloatEdit( ui->tAnim );

	animSlider->addEditor( animSliderEdit );
	animSlider->setParent( ui->tAnim );
	animSlider->setMinimumWidth( 200 );
	animSlider->setMaximumWidth( 500 );
	animSlider->setSizePolicy( QSizePolicy::Minimum, QSizePolicy::MinimumExpanding );

	connect( ogl, &GLView::sceneTimeChanged, animSlider, &FloatSlider::set );
	connect( ogl, &GLView::sceneTimeChanged, animSliderEdit, &FloatEdit::set );
	connect( animSlider, &FloatSlider::valueChanged, ogl, &GLView::setSceneTime );
	connect( animSlider, &FloatSlider::valueChanged, animSliderEdit, &FloatEdit::setValue );
	connect( animSliderEdit, static_cast<void (FloatEdit::*)(float)>(&FloatEdit::sigEdited), ogl, &GLView::setSceneTime );
	connect( animSliderEdit, static_cast<void (FloatEdit::*)(float)>(&FloatEdit::sigEdited), animSlider, &FloatSlider::setValue );
	
	// Animations
	animGroups = new QComboBox( ui->tAnim );
	animGroups->setMinimumWidth( 60 );
	animGroups->setSizeAdjustPolicy( QComboBox::AdjustToContents );
	animGroups->setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Minimum );
	connect( animGroups, static_cast<void (QComboBox::*)(const QString&)>(&QComboBox::activated), ogl, &GLView::setSceneSequence );

	ui->tAnim->addWidget( animSlider );
	animGroupsAction = ui->tAnim->addWidget( animGroups );

	hideAnimToolbar();

	connect( ogl, &GLView::sequencesDisabled, this, &NifSkope::hideAnimToolbar );
	connect( ogl, &GLView::sequenceStopped, ui->aAnimPlay, &QAction::toggle );
	connect( ogl, &GLView::sequenceChanged, [this]( const QString & seqname ) {
		animGroups->setCurrentIndex( ogl->getScene()->animGroups.indexOf( seqname ) );
	} );
	connect( ogl, &GLView::sequencesUpdated, this, &NifSkope::showAnimToolbar );

	connect ( ogl->scene, &Scene::disableSave, [this]() {
		ui->aSaveMenu->setDisabled(true);
		ui->aSave->setDisabled(true);
		ui->aSaveAs->setDisabled(true);
		ui->aReload->setDisabled(true);
	} );

	// LOD Toolbar
	lodSlider = new QSlider( Qt::Horizontal );
	lodSlider->setFocusPolicy( Qt::StrongFocus );
	lodSlider->setTickPosition( QSlider::TicksBelow );
	lodSlider->setTickInterval( 1 );
	lodSlider->setSingleStep( 1 );
	lodSlider->setMinimum( 0 );

	ui->tLOD->addWidget( lodSlider );

	setLodSliderEnabled( false );

	connect( nif, &NifModel::lodSliderChanged, this, &NifSkope::setLodSliderEnabled );
	connect( lodSlider, &QSlider::valueChanged, this, &NifSkope::onLodSliderChange );
}

void NifSkope::initConnections()
{
	connect( this, &NifSkope::beginLoading, this, &NifSkope::onLoadBegin );
	connect( this, &NifSkope::beginSave, this, &NifSkope::onSaveBegin );

	connect( this, &NifSkope::completeLoading, this, &NifSkope::onLoadComplete );
	connect( this, &NifSkope::completeSave, this, &NifSkope::onSaveComplete );
}


QMenu * NifSkope::lightingWidget()
{
	QMenu * mLight = new QMenu( this );
	mLight->setObjectName( "mLight" );
	

	auto lightingWidget = new LightingWidget( ogl, mLight );
	lightingWidget->setActions( {ui->aLighting, ui->aTextures, ui->aVertexColors,
								ui->aSpecular, ui->aCubeMapping, ui->aGlow,
								ui->aVisNormals, ui->aSilhouette} );
	auto aLightingWidget = new QWidgetAction( mLight );
	aLightingWidget->setDefaultWidget( lightingWidget );


	mLight->addAction( aLightingWidget );

	return mLight;
}


QWidget * NifSkope::filePathWidget( QWidget * parent )
{
	// Show Filepath of loaded NIF
	auto filepathWidget = new QWidget( parent );
	filepathWidget->setObjectName( "filepathStatusbarWidget" );
	auto filepathLayout = new QHBoxLayout( filepathWidget );
	filepathWidget->setLayout( filepathLayout );
	filepathLayout->setContentsMargins( 0, 0, 0, 0 );
	auto labelFilepath = new QLabel( "", filepathWidget );
	labelFilepath->setMinimumHeight( 16 );

	filepathLayout->addWidget( labelFilepath );

	// Navigate to Filepath
	auto navigateToFilepath = new QPushButton( "", filepathWidget );
	navigateToFilepath->setFlat( true );
	navigateToFilepath->setIcon( QIcon( ":btn/load" ) );
	navigateToFilepath->setIconSize( QSize( 16, 16 ) );
	navigateToFilepath->setStatusTip( tr( "Show in Explorer" ) );

	filepathLayout->addWidget( navigateToFilepath );

	filepathWidget->setVisible( false );

	// Show Filepath on successful NIF load
	connect( this, &NifSkope::completeLoading, [this, filepathWidget, labelFilepath, navigateToFilepath]( bool success, QString & fname ) {
		filepathWidget->setVisible( success );
		labelFilepath->setText( fname );

		if ( QFileInfo( fname ).exists() ) {
			navigateToFilepath->show();
		} else {
			navigateToFilepath->hide();
		}
	} );

	// Change Filepath on successful NIF save
	connect( this, &NifSkope::completeSave, [this, filepathWidget, labelFilepath, navigateToFilepath]( bool success, QString & fname ) {
		filepathWidget->setVisible( success );
		labelFilepath->setText( fname );

		if ( QFileInfo( fname ).exists() ) {
			navigateToFilepath->show();
		} else {
			navigateToFilepath->hide();
		}
	} );

	// Navigate to NIF in Explorer
	connect( navigateToFilepath, &QPushButton::clicked, [this]() {
#ifdef Q_OS_WIN
		QStringList args;
		args << "/select," << QDir::toNativeSeparators( currentFile );
		QProcess::startDetached( "explorer", args );
#endif
	} );


	return filepathWidget;
}


void NifSkope::archiveDlg()
{
	QString file = QFileDialog::getOpenFileName( this, tr( "Open Archive" ), "", "Archives (*.bsa *.ba2)" );
	if ( !file.isEmpty() )
		openArchive( file );
}

void NifSkope::openDlg()
{
	// Grab most recent filepath if blank window
	auto path = nif->getFileInfo().absolutePath();
	path = (path.isEmpty()) ? recentFileActs[0]->data().toString() : path;

	if ( !saveConfirm() )
		return;

	QStringList files = QFileDialog::getOpenFileNames( this, tr( "Open File" ), path, fileFilters() );
	if ( !files.isEmpty() )
		openFiles( files );
}

void NifSkope::onLoadBegin()
{
	setEnabled( false );
	ogl->setUpdatesEnabled( false );
	ogl->setEnabled( false );

	// Swap out the models with empty versions while loading the file.
	// This is so that the views do not update.
	if ( isInListMode() )
		list->setModel( proxyEmpty );
	else
		list->setModel( nifEmpty );
	tree->setModel( nifEmpty );
	header->setModel( nifEmpty );
	kfmtree->setModel( kfmEmpty );

	animGroups->clear();
	hideAnimToolbar();
	setLodSliderEnabled( false );

	progress->setVisible( true );
	progress->reset();
}

void NifSkope::onLoadComplete( bool success, QString & fname )
{
	QApplication::restoreOverrideCursor();

	int timeout = 2500;
	if ( !success ) {
		// File failed to load
		Message::append( this, NifModel::tr( readFail ), 
						 NifModel::tr( readFailFinal ).arg( fname ), QMessageBox::Critical );

		nif->clear();
		kfm->clear();
		timeout = 0;

		// Remove from Current Files
		clearCurrentFile();

		// Reset
		currentFile.clear();
		updateWindowTitle();
		progress->reset();
	}

	updateFileMenus();

	// Reconnect the models to the views
	tree->setModel( nif );
	setListMode();
	header->setModel( nif );
	resetHeaderSelection();
	kfmtree->setModel( kfm );

	// Mark window as unmodified
	setWindowModified( false );
	nif->undoStack->clear();
	indexStack->clear();

	// Re-enable window
	ogl->setUpdatesEnabled( true );
	ogl->setEnabled( true );
	setEnabled( true ); // IMPORTANT!

	// Hide Progress Bar
	QTimer::singleShot( timeout, progress, SLOT( hide() ) );
}


void NifSkope::saveAsDlg()
{
	QString filename = QFileDialog::getSaveFileName( this, tr( "Save File" ), nif->getFileInfo().absoluteFilePath(),
		fileFilters( false ),
		new QString( fileFilter( nif->getFileInfo().suffix() ) )
	);

	if ( filename.isEmpty() )
		return;

	saveFile( filename );
}

void NifSkope::onSaveBegin()
{
	setEnabled( false );
}

void NifSkope::onSaveComplete( bool success, QString & fname )
{
	setEnabled( true );

	if ( success ) {
		updateWindowTitle();
		// Mark window as unmodified
		nif->undoStack->setClean();
		setWindowModified( false );
	}

	updateFileMenus();
}

bool NifSkope::saveConfirm()
{
	if ( !cfg.suppressSaveConfirm && (isWindowModified() || !nif->undoStack->isClean()) ) {
		QMessageBox::StandardButton response;
		response = QMessageBox::question( this,
			tr( "Save Confirmation" ),
			tr( "<h3><b>You have unsaved changes to %1.</b></h3>Would you like to save them now?" ).arg( nif->getFileInfo().completeBaseName() ),
			QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No );

		if ( response == QMessageBox::Yes ) {
			saveAsDlg();
			return true;
		} else if ( response == QMessageBox::No ) {
			return true;
		} else if ( response == QMessageBox::Cancel ) {
			return false;
		}
	}

	return true;
}

const int WINDOW_STATE_VER = 0x073;

void NifSkope::saveUi() const
{
	QSettings settings;
	// TODO: saveState takes a version number which can be incremented between releases if necessary
	settings.setValue( "Window State"_uip, saveState( WINDOW_STATE_VER ) );
	settings.setValue( "Window Geometry"_uip, saveGeometry() );

	settings.setValue( "Theme", theme );

	settings.setValue( "File/Auto Sanitize", aSanitize->isChecked() );

	settings.setValue( "List Mode"_uip, (isInListMode() ? "list" : "hierarchy") );
	settings.setValue( "Show Non-applicable Rows"_uip, aCondition->isChecked() );

	settings.setValue( "List Header"_uip, list->header()->saveState() );
	settings.setValue( "Tree Header"_uip, tree->header()->saveState() );
	settings.setValue( "Header Header"_uip, header->header()->saveState() );
	settings.setValue( "Kfmtree Header"_uip, kfmtree->header()->saveState() );

	settings.setValue( "GLView/Enable Animations", ui->aAnimate->isChecked() );
	settings.setValue( "GLView/Perspective", ui->aViewPerspective->isChecked() );
}


void NifSkope::restoreUi()
{
	QSettings settings;
	restoreGeometry( settings.value( "Window Geometry"_uip ).toByteArray() );

	// Here goes a workaround for this Qt 5 bug:
	// The positions/sizes of docked widgets of a main window are not restored properly if they were saved while the window was maximized in the previous session.
	// Workaround: if the window is supposed to be maximized, let all the events caused by restoreGeometry to be proccessed before calling restoreState.
	// References:
	//     https://bugreports.qt.io/browse/QTBUG-46620
	//     https://bugreports.qt.io/browse/QTBUG-16252
	//     https://stackoverflow.com/questions/44005852/qdockwidgetrestoregeometry-not-working-correctly-when-qmainwindow-is-maximized 
	// QTBUG-46620 above labeled with "Fix Version/s: 6.3.0 Alpha", so this hack could be not needed after migrating to Qt 6.
	if ( isMaximized() )
		QApplication::processEvents();

	restoreState( settings.value( "Window State"_uip ).toByteArray(), WINDOW_STATE_VER );

	aSanitize->setChecked( settings.value( "File/Auto Sanitize", false ).toBool() );

	if ( settings.value( "List Mode"_uip, "hierarchy" ).toString() == "list" )
		aList->setChecked( true );
	else
		aHierarchy->setChecked( true );

	setListMode();

	aCondition->setChecked( settings.value( "Show Non-applicable Rows"_uip, false ).toBool() );

	list->header()->restoreState( settings.value( "List Header"_uip ).toByteArray() );
	tree->header()->restoreState( settings.value( "Tree Header"_uip ).toByteArray() );
	header->header()->restoreState( settings.value( "Header Header"_uip ).toByteArray() );
	kfmtree->header()->restoreState( settings.value( "Kfmtree Header"_uip ).toByteArray() );

	// Hide advanced metadata loaded from nif.xml as it's not useful or necessary for editing
	auto hideSections = []( NifTreeView * tree, bool hidden ) {
		tree->header()->setSectionHidden( NifModel::ArgCol, hidden );
		tree->header()->setSectionHidden( NifModel::Arr1Col, hidden );
		tree->header()->setSectionHidden( NifModel::Arr2Col, hidden );
		tree->header()->setSectionHidden( NifModel::CondCol, hidden );
		tree->header()->setSectionHidden( NifModel::Ver1Col, hidden );
		tree->header()->setSectionHidden( NifModel::Ver2Col, hidden );
		tree->header()->setSectionHidden( NifModel::VerCondCol, hidden );
	};

	bool bHideMetadatColumns =  settings.value( "Settings/Nif/Hide metadata columns", true ).toBool();
	hideSections( tree, bHideMetadatColumns );
	hideSections( header, bHideMetadatColumns );

	ui->aAnimate->setChecked( settings.value( "GLView/Enable Animations", true ).toBool() );

	auto isPersp = settings.value( "GLView/Perspective", true ).toBool();
	ui->aViewPerspective->setChecked( isPersp );

	ogl->setProjection( isPersp );

	QVariant fontVar = settings.value( "UI/View Font" );

	if ( fontVar.canConvert<QFont>() )
		setViewFont( fontVar.value<QFont>() );

	// Modify UI settings that cannot be set in Designer
	tabifyDockWidget( ui->InspectDock, ui->KfmDock );
}

void NifSkope::updateFileMenus()
{
	ui->aSaveMenu->setEnabled( true );
	ui->aSave->setEnabled( true );
	ui->aSaveAs->setEnabled( true );
	ui->aReload->setEnabled( !getCurrentFile().isEmpty() );

	updateImportExportMenu(mExport);
	updateImportExportMenu(mImport);
}

void NifSkope::resetHeaderSelection()
{
	auto headerIndex = nif->getHeaderIndex();
	header->setRootIndex( headerIndex );
	int iLastChild = std::max( nif->rowCount( headerIndex ) - 1, 0 );
	header->updateConditions( headerIndex.child( 0, 0 ), headerIndex.child( iLastChild, 0 ) );
	header->autoExpandBlock( headerIndex );
}

void NifSkope::setViewFont( const QFont & font )
{
	list->setFont( font );
	QFontMetrics metrics( list->font() );
	list->setIconSize( QSize( metrics.width( "000" ), metrics.lineSpacing() ) );
	tree->setFont( font );
	tree->setIconSize( QSize( metrics.width( "000" ), metrics.lineSpacing() ) );
	header->setFont( font );
	header->setIconSize( QSize( metrics.width( "000" ), metrics.lineSpacing() ) );
	kfmtree->setFont( font );
	kfmtree->setIconSize( QSize( metrics.width( "000" ), metrics.lineSpacing() ) );
	ogl->setFont( font );
}

void NifSkope::reloadTheme()
{
	for ( QWidget * widget : QApplication::topLevelWidgets() ) {
		NifSkope * win = qobject_cast<NifSkope *>(widget);
		if ( win ) {
			win->loadTheme();
		}
	}
}

void NifSkope::loadTheme()
{
	QSettings settings;
	theme = WindowTheme( settings.value( "Theme", ThemeDark ).toInt() );
	ui->mTheme->actions()[theme]->setChecked( true );

	toolbarSize = ToolbarSize( settings.value( "Settings/Theme/Large Icons", ToolbarLarge ).toBool() );

	setToolbarSize();

	switch ( theme )
	{
	case ThemeWindowsXP:
		QApplication::setStyle( QStyleFactory::create( "WindowsXP" ) );
		qApp->setStyleSheet("");
		qApp->setPalette( style()->standardPalette() );
		return;
	case ThemeWindows:
		QApplication::setStyle( QStyleFactory::create( "WindowsVista" ) );
		qApp->setStyleSheet("");
		qApp->setPalette( style()->standardPalette() );
		return;
	case ThemeDark:
	case ThemeLight:
	default:
		QApplication::setStyle( QStyleFactory::create( "Fusion" ) );
	}

	QPalette pal;
	auto baseC = settings.value( "Settings/Theme/Base Color", defaultsDark[Base] ).value<QColor>();
	auto baseCAlt = settings.value( "Settings/Theme/Base Color Alt", defaultsDark[BaseAlt] ).value<QColor>();
	auto baseCTxt = settings.value( "Settings/Theme/Text", defaultsDark[Text] ).value<QColor>();
	auto baseCHighlight = settings.value( "Settings/Theme/Highlight", defaultsDark[Highlight] ).value<QColor>();
	auto baseCTxtHighlight = settings.value( "Settings/Theme/Highlight Text", defaultsDark[HighlightText] ).value<QColor>();
	auto baseCBrightTxt = settings.value( "Settings/Theme/Bright Text", defaultsDark[BrightText] ).value<QColor>();

	// Fill the standard palette
	pal.setColor( QPalette::Window, baseC );
	pal.setColor( QPalette::WindowText, baseCTxt );
	pal.setColor( QPalette::Base, baseC );
	pal.setColor( QPalette::AlternateBase, baseCAlt );
	pal.setColor( QPalette::ToolTipBase, baseC );
	pal.setColor( QPalette::ToolTipText, baseCTxt );
	pal.setColor( QPalette::Text, baseCTxt );
	pal.setColor( QPalette::Button, baseC );
	pal.setColor( QPalette::ButtonText, baseCTxt );
	pal.setColor( QPalette::BrightText, baseCBrightTxt );
	pal.setColor( QPalette::Link, baseCBrightTxt );
	pal.setColor( QPalette::Highlight, baseCHighlight );
	pal.setColor( QPalette::HighlightedText, baseCTxtHighlight );

	// Mute the disabled palette
	auto baseCDark = baseC.darker( 150 );
	auto baseCAltDark = baseCAlt.darker( 150 );
	auto baseCHighlightDark = QColor( 128, 128, 128 );
	auto baseCTxtDark = Qt::darkGray;
	auto baseCTxtHighlightDark = Qt::darkGray;
	auto baseCBrightTxtDark = Qt::darkGray;

	pal.setColor( QPalette::ColorGroup::Disabled, QPalette::Window, baseC ); // Leave base color the same
	pal.setColor( QPalette::ColorGroup::Disabled, QPalette::WindowText, baseCTxtDark );
	pal.setColor( QPalette::ColorGroup::Disabled, QPalette::Base, baseCDark );
	pal.setColor( QPalette::ColorGroup::Disabled, QPalette::AlternateBase, baseCAltDark );
	pal.setColor( QPalette::ColorGroup::Disabled, QPalette::ToolTipBase, baseCDark );
	pal.setColor( QPalette::ColorGroup::Disabled, QPalette::ToolTipText, baseCTxtDark );
	pal.setColor( QPalette::ColorGroup::Disabled, QPalette::Text, baseCTxtDark );
	pal.setColor( QPalette::ColorGroup::Disabled, QPalette::Button, baseCDark );
	pal.setColor( QPalette::ColorGroup::Disabled, QPalette::ButtonText, baseCTxtDark );
	pal.setColor( QPalette::ColorGroup::Disabled, QPalette::BrightText, baseCBrightTxtDark );
	pal.setColor( QPalette::ColorGroup::Disabled, QPalette::Link, baseCBrightTxtDark );
	pal.setColor( QPalette::ColorGroup::Disabled, QPalette::Highlight, baseCHighlightDark );
	pal.setColor( QPalette::ColorGroup::Disabled, QPalette::HighlightedText, baseCTxtHighlightDark );

	// Set Palette and Stylesheet
	
	QDir qssDir( QApplication::applicationDirPath() );
	QStringList qssList( QStringList()
						 << "style.qss"
#ifdef Q_OS_LINUX
						 << "/usr/share/nifskope/style.qss"
#endif
	);
	QString qssName;
	for ( const QString& str : qssList ) {
		if ( qssDir.exists( str ) ) {
			qssName = qssDir.filePath( str );
			break;
		}
	}

	// Load stylesheet
	QString styleData;
	if ( !qssName.isEmpty() ) {
		QFile style( qssName );
		if ( style.open( QFile::ReadOnly ) ) {
			styleData = style.readAll();
			style.close();
		}
	}

	// Remove comments first
	QRegularExpression cssComment( R"regex(\/\*[^*]*\*+([^/*][^*]*\*+)*\/)regex" );
	styleData.replace( cssComment, "" );

	// Theme name for icon path customization
	styleData.replace( "${theme}", (theme == ThemeDark) ? "dark" : "light" );

	// Highlight colors in an "R, G, B" string to combine with opacity in rgba()
	auto rgb = QString("%1, %2, %3").arg(baseCHighlight.red())
									.arg(baseCHighlight.green())
									.arg(baseCHighlight.blue());
	styleData.replace( "${rgb}", rgb );

	qApp->setPalette( pal );
	qApp->setStyleSheet( styleData );
}

void NifSkope::setToolbarSize()
{
	QSize size = {18, 18};
	if ( toolbarSize == ToolbarLarge )
		size = {36, 36};

	for ( QObject * o : children() ) {
		auto tb = qobject_cast<QToolBar *>(o);
		if ( tb )
			tb->setIconSize(size);
	}
}

void NifSkope::setTheme( nstheme::WindowTheme t )
{
	theme = t;

	QSettings settings;
	settings.setValue( "Theme", theme );

	QColor * defaults = nullptr;
	QString iconPrefix;

	// If Dark reset to dark colors and icons
	// If Light reset to light colors and icons
	switch ( t ) {
	case ThemeDark:
		defaults = defaultsDark;
		break;
	case ThemeLight:
		defaults = defaultsLight;
		break;
	default:
		break;
	}

	if ( defaults ) {
		settings.setValue( "Settings/Theme/Base Color", defaults[Base] );
		settings.setValue( "Settings/Theme/Base Color Alt", defaults[BaseAlt] );
		settings.setValue( "Settings/Theme/Text", defaults[Text] );
		settings.setValue( "Settings/Theme/Highlight", defaults[Highlight] );
		settings.setValue( "Settings/Theme/Highlight Text", defaults[HighlightText] );
		settings.setValue( "Settings/Theme/Bright Text", defaults[BrightText] );
	}

	loadTheme();
}

void NifSkope::resizeDone()
{
	isResizing = false;

	// Unhide GLView, update GLGraphicsView
	ogl->show();
	graphicsScene->setSceneRect( graphicsView->rect() );
	graphicsView->fitInView( graphicsScene->sceneRect() );

	ogl->setUpdatesEnabled( true );
	ogl->setDisabled( false );
	ogl->getScene()->animate = true;
	auto viewSize = graphicsView->size();
	ogl->resize( viewSize.width(), viewSize.height() );
	ogl->resizeGL( viewSize.width(), viewSize.height() );
}

void NifSkope::setLodSliderEnabled( bool enabled )
{
	auto tLOD = ui->tLOD;

	if ( enabled ) {
		Scene * scene = ogl->getScene();
		if ( !scene ) { // Just in case
			enabled = false;
		} else if ( !tLOD->isEnabled() ) {
			lodSlider->setMaximum( scene->maxLodLevel() );
			lodSlider->setValue( scene->lodLevel );
		}
	}

	tLOD->setEnabled( enabled );
	tLOD->setVisible( enabled );
}

void NifSkope::onLodSliderChange( int newLodLevel )
{
	if ( ui->tLOD->isEnabled() ) {
		Scene * scene = ogl->getScene();
		if ( scene ) {
			scene->updateLodLevel( newLodLevel );
			ogl->updateGL();
		}
	}
}

void NifSkope::hideAnimToolbar()
{
	ogl->resetAnimation();
	ui->tAnim->setEnabled( false );
	ui->tAnim->hide();
}

void NifSkope::showAnimToolbar()
{
	ui->tAnim->setEnabled( true );
	ui->tAnim->show();

	animGroups->clear();
	animGroups->addItems( ogl->getScene()->animGroups );
	animGroups->setCurrentIndex( ogl->getScene()->animGroups.indexOf( ogl->getScene()->animGroup ) );

	if ( animGroups->count() == 0 ) {
		animGroupsAction->setVisible( false );
		ui->aAnimSwitch->setVisible( false );
	} else {
		ui->aAnimSwitch->setVisible( animGroups->count() != 1 );
		animGroupsAction->setVisible( true );
		animGroups->adjustSize();
	}
}

bool NifSkope::eventFilter( QObject * o, QEvent * e )
{
	// TODO: This doesn't seem to be doing anything extra
	//if ( e->type() == QEvent::Polish ) {
	//	QTimer::singleShot( 0, this, SLOT( overrideViewFont() ) );
	//}

	// Global mouse press
	if ( o->isWindowType() && e->type() == QEvent::MouseButtonPress ) {
		//qDebug() << "Mouse Press";
	}
	// Global mouse release
	if ( o->isWindowType() && e->type() == QEvent::MouseButtonRelease ) {
		//qDebug() << "Mouse Release";

		// Back/Forward button support for cycling through indices
		auto mouseEvent = static_cast<QMouseEvent *>(e);
		if ( mouseEvent ) {
			if ( mouseEvent->button() == Qt::ForwardButton ) {
				mouseEvent->accept();
				indexStack->redo();
			}

			if ( mouseEvent->button() == Qt::BackButton ) {
				mouseEvent->accept();
				indexStack->undo();
			}
		}
	}

	// Filter GLGraphicsView
	auto obj = qobject_cast<GLGraphicsView *>(o);
	if ( !obj || obj != graphicsView )
		return QMainWindow::eventFilter( o, e );

	// Turn off animation
	// Grab framebuffer
	// Begin resize timer
	// Block all Resize Events to GLView
	if ( e->type() == QEvent::Resize ) {
		// Hide GLView
		ogl->hide();

		if ( !isResizing  && !resizeTimer->isActive() ) {
			ogl->getScene()->animate = false;
			ogl->updateGL();

			if ( viewBuffer.isNull() ) {
				// Init initial buffer with solid color
				//	Otherwise becomes random colors on release builds
				viewBuffer = QImage( 10, 10, QImage::Format_ARGB32 );
				viewBuffer.fill( ogl->clearColor() );
			} else {
				viewBuffer = ogl->grabFrameBuffer();
			}

			ogl->setUpdatesEnabled( false );
			ogl->setDisabled( true );

			isResizing = true;
		}

		resizeTimer->start( 300 );

		return true;
	}

	// Paint stored framebuffer over GLGraphicsView while resizing
	if ( !viewBuffer.isNull() && isResizing && e->type() == QEvent::Paint ) {
		QPainter painter;
		painter.begin( graphicsView );
		painter.drawImage( QRect( 0, 0, painter.device()->width(), painter.device()->height() ), viewBuffer );
		painter.end();

		return true;
	}

	return QMainWindow::eventFilter( o, e );
}


/*
* Slots
* **********************
*/


void NifSkope::contextMenu( const QPoint & pos )
{
	QModelIndex idx;
	QPoint p = pos;

	if ( sender() == tree ) {
		idx = tree->indexAt( pos );
		p = tree->mapToGlobal( pos );
	} else if ( sender() == list ) {
		idx = list->indexAt( pos );
		p = list->mapToGlobal( pos );
	} else if ( sender() == header ) {
		idx = header->indexAt( pos );
		p = header->mapToGlobal( pos );
	} else if ( sender() == graphicsView ) {
		idx = ogl->indexAt( pos );
		p = graphicsView->mapToGlobal( pos );
	} else {
		return;
	}

	while ( idx.model() && idx.model()->inherits( "NifProxyModel" ) ) {
		idx = qobject_cast<const NifProxyModel *>(idx.model())->mapTo( idx );
	}

	SpellBook contextBook( nif, idx, this, SLOT( select( const QModelIndex & ) ) );

	if ( !idx.isValid() || nif->flags( idx ) & (Qt::ItemIsEnabled | Qt::ItemIsSelectable) )
		contextBook.exec( p );
}

void NifSkope::overrideViewFont()
{
	QSettings settings;
	QVariant var = settings.value( "UI/View Font" );

	if ( var.canConvert<QFont>() ) {
		setViewFont( var.value<QFont>() );
	}
}


/*
* Automatic Slots
* **********************
*/


void NifSkope::on_aLoadXML_triggered()
{
	NifModel::loadXML();
	KfmModel::loadXML();
}

void NifSkope::on_aReload_triggered()
{
	if ( NifModel::loadXML() ) {
		reload();
	}
}

void NifSkope::on_aSelectFont_triggered()
{
	bool ok;
	QFont fnt = QFontDialog::getFont( &ok, list->font(), this );

	if ( !ok )
		return;

	setViewFont( fnt );
	QSettings settings;
	settings.setValue( "UI/View Font", fnt );
}

void NifSkope::on_aWindow_triggered()
{
	createWindow();
}

void NifSkope::on_aShredder_triggered()
{
	TestShredder::create();
}

void NifSkope::on_aHeader_triggered()
{
	if ( tree )
		tree->clearRootIndex();

	select( nif->getHeaderIndex() );
}

void NifSkope::on_tRender_actionTriggered( [[maybe_unused]] QAction * action )
{
}

void NifSkope::on_aViewTop_triggered( bool checked )
{
	if ( checked )
		ogl->setViewMode( GLView::ViewTop );
}

void NifSkope::on_aViewFront_triggered( bool checked )
{
	if ( checked )
		ogl->setViewMode( GLView::ViewFront );
}

void NifSkope::on_aViewLeft_triggered( bool checked )
{
	if ( checked )
		ogl->setViewMode( GLView::ViewLeft );
}

void NifSkope::on_aViewCenter_triggered()
{
	ogl->center();
}

void NifSkope::on_aViewFlip_triggered( [[maybe_unused]] bool checked )
{
	ogl->flipView();
}

void NifSkope::on_aViewPerspective_toggled( bool checked )
{
	ogl->setProjection( checked );
}

void NifSkope::on_aViewWalk_triggered( bool checked )
{
	if ( checked )
		ogl->setViewMode( GLView::ViewWalk );
}

void NifSkope::on_aViewUserSave_triggered( [[maybe_unused]] bool checked )
{ 
	ogl->saveUserView();
}

void NifSkope::on_aViewUser_toggled( bool checked )
{
	if ( checked )
		ogl->setViewMode( GLView::ViewUser );
}

void NifSkope::on_aSettings_triggered()
{
	options->show();
	options->raise();
	options->activateWindow();
}

void NifSkope::on_mTheme_triggered( QAction * action )
{
	auto newTheme = WindowTheme( action->data().toInt() );

	setTheme( newTheme );
}

void NifSkope::updateCurrentViewAction()
{
	QAction * pSelectedView = nullptr;

	switch( ogl->view ) {
	case GLView::ViewFront:
		pSelectedView = ui->aViewFront;
		break;
	case GLView::ViewLeft:
		pSelectedView = ui->aViewLeft;
		break;
	case GLView::ViewTop:
		pSelectedView = ui->aViewTop;
		break;
	case GLView::ViewWalk:
		pSelectedView = ui->aViewWalk;
		break;
	case GLView::ViewUser:
		pSelectedView = ui->aViewUser;
		break;
	}

	if ( pSelectedView ) {
		pSelectedView->setChecked( true );
	} else if ( viewActions ) {
		pSelectedView = viewActions->checkedAction();
		if ( pSelectedView )
			pSelectedView->setChecked( false );
	}
}
