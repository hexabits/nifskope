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

#include "glview.h"

#include "message.h"
#include "nifskope.h"
#include "gl/renderer.h"
#include "gl/glshape.h"
#include "gl/gltex.h"
#include "model/nifmodel.h"
#include "ui/settingsdialog.h"
#include "ui/widgets/fileselect.h"
#include "ui/UiUtils.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QImageWriter>
#include <QMimeData>
#include <QMouseEvent>
#include <QTimer>

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QGLFormat>

// TODO: Determine the necessity of this
// Appears to be used solely for gluErrorString
// There may be some Qt alternative
#ifdef __APPLE__
	#include <OpenGL/glu.h>
#else
	#include <GL/glu.h>
#endif


// NOTE: The FPS define is a frame limiter,
//	NOT the guaranteed FPS in the viewport.
//	Also the QTimer is integer milliseconds 
//	so 60 will give you 1000/60 = 16, not 16.666
//	therefore it's really 62.5FPS
#define FPS 144

#define ZOOM_MIN 1.0
#define ZOOM_MAX 1000.0
#define ZOOM_PAGE_KEY_MULT 1.025

#define ZOOM_QE_KEY_MULT 1.025 
#define ZOOM_MOUSE_WHEEL_MULT 0.95


//! @file glview.cpp GLView implementation


GLGraphicsView::GLGraphicsView( QWidget * parent ) : QGraphicsView()
{
	setContextMenuPolicy( Qt::CustomContextMenu );
	setFocusPolicy( Qt::ClickFocus );
	setAcceptDrops( true );
	
	installEventFilter( parent );
}

GLGraphicsView::~GLGraphicsView() {}


GLView * GLView::create( NifSkope * window )
{
	QGLFormat fmt;
	static QList<QPointer<GLView> > views;

	QGLWidget * share = nullptr;
	for ( const QPointer<GLView>& v : views ) {
		if ( v )
			share = v;
	}

	QSettings settings;
	int aa = settings.value( "Settings/Render/General/Antialiasing", 4 ).toInt();

	// All new windows after the first window will share a format
	if ( share ) {
		fmt = share->format();
	} else {
		fmt.setSampleBuffers( aa > 0 );
	}
	
	// OpenGL version
	fmt.setVersion( 2, 1 );
	// Ignored if version < 3.2
	//fmt.setProfile(QGLFormat::CoreProfile);

	// V-Sync
	fmt.setSwapInterval( 1 );
	fmt.setDoubleBuffer( true );

	fmt.setSamples( std::pow( aa, 2 ) );

	fmt.setDirectRendering( true );
	fmt.setRgba( true );

	views.append( QPointer<GLView>( new GLView( fmt, window, share ) ) );

	return views.last();
}

GLView::GLView( const QGLFormat & format, QWidget * p, const QGLWidget * shareWidget )
	: QGLWidget( format, p, shareWidget )
{
	setFocusPolicy( Qt::ClickFocus );
	//setAttribute( Qt::WA_PaintOnScreen );
	//setAttribute( Qt::WA_NoSystemBackground );
	setAutoFillBackground( false );
	setAcceptDrops( true );
	setContextMenuPolicy( Qt::CustomContextMenu );

	// Manually handle the buffer swap
	// Fixes bug with QGraphicsView and double buffering
	//	Input becomes sluggish and CPU usage doubles when putting GLView
	//	inside a QGraphicsView.
	setAutoBufferSwap( false );

	// Make the context current on this window
	makeCurrent();
	if ( !isValid() )
		return;

	// Create an OpenGL context
	glContext = context()->contextHandle();

	// Obtain a functions object and resolve all entry points
	glFuncs = glContext->functions();

	if ( !glFuncs ) {
		Message::critical( this, tr( "Could not obtain OpenGL functions" ) );
		exit( 1 );
	}

	glFuncs->initializeOpenGLFunctions();

	view = ViewDefault;
	animState = AnimEnabled;
	debugMode = DbgNone;

	Zoom = 1.0;

	doCenter  = false;
	doCompile = false;

	model = nullptr;

	time = 0.0;
	lastTime = QTime::currentTime();

	textures = new TexCache( this );

	updateSettings();

	scene = new Scene( textures, glContext, glFuncs );
	connect( textures, &TexCache::sigRefresh, this, static_cast<void (GLView::*)()>(&GLView::update) );
	connect( scene, &Scene::sceneUpdated, this, static_cast<void (GLView::*)()>(&GLView::update) );

	timer = new QTimer( this );
	timer->setInterval( 1000 / FPS );
	timer->start();
	connect( timer, &QTimer::timeout, this, &GLView::advanceGears );

	lightVisTimeout = 1500;
	lightVisTimer = new QTimer( this );
	lightVisTimer->setSingleShot( true );
	connect( lightVisTimer, &QTimer::timeout, [this]() { setVisMode( Scene::VisLightPos, false ); update(); } );

	connect( NifSkope::getOptions(), &SettingsDialog::flush3D, textures, &TexCache::flush );

	connect(NifSkope::getOptions(), &SettingsDialog::update3D, this, static_cast<void (GLView::*)()>(&GLView::updateSettings));
	connect(NifSkope::getOptions(), &SettingsDialog::update3D, [this]() {
		// Calling update() here in a lambda can crash..
		//updateSettings();
		qglClearColor(clearColor());
		//update();
	});
	connect(NifSkope::getOptions(), &SettingsDialog::update3D, this, static_cast<void (GLView::*)()>(&GLView::update));
}

GLView::~GLView()
{
	flush();

	delete textures;
	delete scene;
}

void GLView::updateSettings()
{
	QSettings settings;
	settings.beginGroup( "Settings/Render" );

	cfg.background = settings.value( "Colors/Background", QColor( 46, 46, 46 ) ).value<QColor>();
	cfg.fov = settings.value( "General/Camera/Field Of View" ).toFloat();
	cfg.moveSpd = settings.value( "General/Camera/Movement Speed" ).toFloat();
	cfg.rotSpd = settings.value( "General/Camera/Rotation Speed" ).toFloat();
	cfg.upAxis = UpAxis(settings.value( "General/Up Axis", ZAxis ).toInt());

	settings.endGroup();
}

QColor GLView::clearColor() const
{
	return cfg.background;
}


/* 
 * Scene
 */

Scene * GLView::getScene()
{
	return scene;
}

void GLView::updateScene()
{
	scene->update( model, QModelIndex() );
	update();
}

void GLView::updateAnimationState( bool checked )
{
	QAction * action = qobject_cast<QAction *>(sender());
	if ( action ) {
		auto opt = AnimationState( action->data().toInt() );

		if ( checked )
			animState |= opt;
		else
			animState &= ~opt;

		scene->animate = (animState & AnimEnabled);
		lastTime = QTime::currentTime();

		update();
	}
}


/*
 *  OpenGL
 */

void GLView::initializeGL()
{
	GLenum err;
	
	if ( scene->hasOption(Scene::DoMultisampling) ) {
		if ( !glContext->hasExtension( "GL_EXT_framebuffer_multisample" ) ) {
			scene->options &= ~Scene::DoMultisampling;
			//qDebug() << "System does not support multisampling";
		} /* else {
			GLint maxSamples;
			glGetIntegerv( GL_MAX_SAMPLES, &maxSamples );
			qDebug() << "Max samples:" << maxSamples;
		}*/
	}

	initializeTextureUnits( glContext );

	if ( scene->renderer->initialize() )
		updateShaders();

	// Initial viewport values
	//	Made viewport and aspect member variables.
	//	They were being updated every single frame instead of only when resizing.
	//glGetIntegerv( GL_VIEWPORT, viewport );
	if ( viewportWidth < 0 )
		cacheViewportSize();

	// Check for errors
	while ( ( err = glGetError() ) != GL_NO_ERROR )
		qDebug() << tr( "glview.cpp - GL ERROR (init) : " ) << (const char *)gluErrorString( err );
}

void GLView::updateShaders()
{
	makeCurrent();
	if ( !isValid() )
		return;
	scene->updateShaders();
	update();
}

void GLView::glProjection( [[maybe_unused]] int x, [[maybe_unused]] int y )
{
	glMatrixMode( GL_PROJECTION );
	glLoadIdentity();

	BoundSphere bs = scene->view * scene->bounds();

	if ( scene->hasOption(Scene::ShowAxes) ) {
		bs |= BoundSphere( scene->view * Vector3(), axis );
	}

	float bounds = (bs.radius > 1024.0 * scale()) ? bs.radius : 1024.0 * scale();


	GLdouble nr = fabs( bs.center[2] ) - bounds * 1.5;
	GLdouble fr = fabs( bs.center[2] ) + bounds * 1.5;

	if ( perspectiveMode || view == ViewWalk ) {
		// Perspective View
		if ( nr < 1.0 * scale() )
			nr = 1.0 * scale();
		if ( fr < 2.0 * scale() )
			fr = 2.0 * scale();

		if ( nr > fr ) {
			// add: swap them when needed
			std::swap( nr, fr );
		}

		if ( (fr - nr) < 0.00001 ) {
			// add: ensure distance
			nr = 1.0 * scale();
			fr = 2.0 * scale();
		}

		GLdouble h2 = tan( double( cfg.fov ) * M_PI / ( Zoom * 360.0 ) ) * nr;
		GLdouble w2 = h2 * aspectWidth / aspectHeight;
		glFrustum( -w2, +w2, -h2, +h2, nr, fr );
	} else {
		// Orthographic View
		GLdouble h2 = Dist / Zoom;
		GLdouble w2 = h2 * aspectWidth / aspectHeight;
		glOrtho( -w2, +w2, -h2, +h2, nr, fr );
	}

	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();
}


#ifdef USE_GL_QPAINTER
void GLView::paintEvent( QPaintEvent * event )
{
	makeCurrent();
	if ( !isValid() )
		return;

	QPainter painter;
	painter.begin( this );
	painter.setRenderHint( QPainter::TextAntialiasing );
#else
void GLView::paintGL()
{
#endif
	
	// Save GL state
	glPushAttrib( GL_ALL_ATTRIB_BITS );
	glMatrixMode( GL_PROJECTION );
	glPushMatrix();
	glMatrixMode( GL_MODELVIEW );
	glPushMatrix();

	// Clear Viewport
	if ( scene->hasVisMode(Scene::VisSilhouette) ) {
		qglClearColor( QColor( 255, 255, 255, 255 ) );
	}

	glDisable(GL_FRAMEBUFFER_SRGB);
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
	
	
	// Compile the model
	if ( doCompile ) {
		textures->setNifFolder( model->getFolder() );
		scene->make( model );

		auto tMin = scene->timeMin();
		auto tMax = scene->timeMax();

		scene->transform( Transform(), tMin );

		axis = (scene->bounds().radius <= 0) ? 1024.0 * scale() : scene->bounds().radius;

		if ( doTimerReset )
			time = tMin;
		if ( tMin != tMax ) {
			if ( time < tMin || time > tMax  )
				time = tMin;

			emit sequencesUpdated();

		} else if ( tMax == 0 ) {
			// No Animations in this NIF
			emit sequencesDisabled( true );
		}
		emit sceneTimeChanged( time, tMin, tMax );
		doCompile = false;
		doTimerReset = false;
	}

	// Center the model
	if ( doCenter ) {
		setCenter();
		doCenter = false;
	}

	// Transform the scene
	Matrix ap;

	if ( cfg.upAxis == YAxis ) {
		ap( 0, 0 ) = 0; ap( 0, 1 ) = 0; ap( 0, 2 ) = 1;
		ap( 1, 0 ) = 1; ap( 1, 1 ) = 0; ap( 1, 2 ) = 0;
		ap( 2, 0 ) = 0; ap( 2, 1 ) = 1; ap( 2, 2 ) = 0;
	} else if ( cfg.upAxis == XAxis ) {
		ap( 0, 0 ) = 0; ap( 0, 1 ) = 1; ap( 0, 2 ) = 0;
		ap( 1, 0 ) = 0; ap( 1, 1 ) = 0; ap( 1, 2 ) = 1;
		ap( 2, 0 ) = 1; ap( 2, 1 ) = 0; ap( 2, 2 ) = 0;
	}

	viewTrans.rotation.fromEuler( deg2rad(Rot[0]), deg2rad(Rot[1]), deg2rad(Rot[2]) );
	viewTrans.translation = viewTrans.rotation * Pos;
	viewTrans.rotation = viewTrans.rotation * ap;

	if ( view != ViewWalk )
		viewTrans.translation[2] -= Dist * 2;

	scene->transform( viewTrans, time );

	// Setup projection mode
	glProjection();
	glLoadIdentity();

	// Draw the grid
	if ( scene->hasOption(Scene::ShowGrid) ) {
		glDisable( GL_ALPHA_TEST );
		glDisable( GL_BLEND );
		glDisable( GL_LIGHTING );
		glDisable( GL_COLOR_MATERIAL );
		glEnable( GL_DEPTH_TEST );
		glDepthMask( GL_TRUE );
		glDepthFunc( GL_LESS );
		glDisable( GL_TEXTURE_2D );
		glDisable( GL_NORMALIZE );
		glLineWidth( 2.0f );

		// Keep the grid "grounded" regardless of Up Axis
		Transform gridTrans = viewTrans;
		if ( cfg.upAxis != ZAxis )
			gridTrans.rotation = viewTrans.rotation * ap.inverted();

		glPushMatrix();
		glLoadMatrix( gridTrans );

		// TODO: Configurable grid in Settings
		// 1024 game units, major lines every 128, minor lines every 64
		drawGrid( (int)(1024 * scale()), (int)(128 * scale()), 2);

		glPopMatrix();
	}

#ifndef QT_NO_DEBUG
	// Debug scene bounds
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_TRUE );
	glDepthFunc( GL_LESS );
	glPushMatrix();
	glLoadMatrix( viewTrans );
	if ( debugMode == DbgBounds ) {
		BoundSphere bs = scene->bounds();
		bs |= BoundSphere( Vector3(), axis );
		drawSphere( bs.center, bs.radius );
	}
	glPopMatrix();
#endif

	GLfloat mat_spec[] = { 0.0f, 0.0f, 0.0f, 1.0f };

	if ( scene->hasOption(Scene::DoLighting) ) {
		// Setup light
		Vector4 lightDir( 0.0, 0.0, 1.0, 0.0 );

		if ( !frontalLight ) {
			float decl = deg2rad( declination );
			Vector3 v( sin( decl ), 0, cos( decl ) );
			Matrix m; m.fromEuler( 0, 0, deg2rad( planarAngle ) );
			v = m * v;
			lightDir = Vector4( viewTrans.rotation * v, 0.0 );

			if ( scene->hasVisMode(Scene::VisLightPos) ) {
				glEnable( GL_BLEND );
				glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
				glEnable( GL_DEPTH_TEST );
				glDepthMask( GL_TRUE );
				glDepthFunc( GL_LESS );

				glPushMatrix();
				glLoadMatrix( viewTrans );

				glLineWidth( 2.0f );
				glColor4f( 1.0f, 1.0f, 1.0f, 0.5f );

				// Scale the distance a bit
				float l = axis + 64.0;
				l = (l < 128) ? axis * 1.5 : l;
				l = (l > 2048) ? axis * 0.66 : l;
				l = (l > 1024) ? axis * 0.75 : l;

				drawDashLine( Vector3( 0, 0, 0 ), v * l, 30 );
				drawSphere( v * l, axis / 10 );
				glPopMatrix();
				glDisable( GL_BLEND );
			}
		}

		float amb = ambient;
		if ( scene->hasVisMode(Scene::VisNormalsOnly) && scene->hasOption(Scene::DoTexturing) && !scene->hasOption(Scene::DisableShaders) ) {
			amb = 0.1f;
		}
		
		GLfloat mat_amb[] = { amb, amb, amb, 1.0f };
		GLfloat mat_diff[] = { brightness, brightness, brightness, 1.0f };
		

		glShadeModel( GL_SMOOTH );
		//glEnable( GL_LIGHTING );
		glEnable( GL_LIGHT0 );
		glLightfv( GL_LIGHT0, GL_AMBIENT, mat_amb );
		glLightfv( GL_LIGHT0, GL_DIFFUSE, mat_diff );
		glLightfv( GL_LIGHT0, GL_SPECULAR, mat_diff );
		glLightfv( GL_LIGHT0, GL_POSITION, lightDir.data() );
	} else {
		float amb = scene->hasOption(Scene::DisableShaders) ? 0.0f : 0.5f;

		GLfloat mat_amb[] = { amb, amb, amb, 1.0f };
		GLfloat mat_diff[] = { 1.0f, 1.0f, 1.0f, 1.0f };
		

		glShadeModel( GL_SMOOTH );
		//glEnable( GL_LIGHTING );
		glEnable( GL_LIGHT0 );
		glLightfv( GL_LIGHT0, GL_AMBIENT, mat_amb );
		glLightfv( GL_LIGHT0, GL_DIFFUSE, mat_diff );
		glLightfv( GL_LIGHT0, GL_SPECULAR, mat_spec );
	}

	if ( scene->hasVisMode(Scene::VisSilhouette) ) {
		GLfloat mat_diff[] = { 0.0f, 0.0f, 0.0f, 1.0f };
		GLfloat mat_amb[] = { 0.0f, 0.0f, 0.0f, 1.0f };

		glShadeModel( GL_FLAT );
		//glEnable( GL_LIGHTING );
		glEnable( GL_LIGHT0 );
		glLightModelfv( GL_LIGHT_MODEL_AMBIENT, mat_diff );
		glMaterialfv( GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, mat_diff );
		glLightfv( GL_LIGHT0, GL_AMBIENT, mat_amb );
		glLightfv( GL_LIGHT0, GL_DIFFUSE, mat_diff );
		glLightfv( GL_LIGHT0, GL_SPECULAR, mat_spec );
	}

	if ( scene->hasOption(Scene::DoMultisampling) )
		glEnable( GL_MULTISAMPLE_ARB );

#ifndef QT_NO_DEBUG
	// Color Key debug
	if ( debugMode == DbgColorPicker ) {
		glDisable( GL_MULTISAMPLE );
		glDisable( GL_LINE_SMOOTH );
		glDisable( GL_TEXTURE_2D );
		glDisable( GL_BLEND );
		glDisable( GL_DITHER );
		glDisable( GL_LIGHTING );
		glShadeModel( GL_FLAT );
		glDisable( GL_FOG );
		glDisable( GL_MULTISAMPLE_ARB );
		glEnable( GL_DEPTH_TEST );
		glDepthFunc( GL_LEQUAL );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
		Node::SELECTING = 1;
	} else {
		Node::SELECTING = 0;
	}
#endif

	// Draw the model
	scene->draw();

	if ( scene->hasOption(Scene::ShowAxes) ) {
		// Resize viewport to small corner of screen
		glViewport( 0, 0, axesSize, axesSize );

		// Reset matrices
		glMatrixMode( GL_PROJECTION );
		glLoadIdentity();

		// Square frustum
		double nr = 1.0;
		double fr = 250.0;
		GLdouble h2 = tan( double( cfg.fov ) * M_PI / 360.0 ) * nr;
		GLdouble w2 = h2;
		glFrustum( -w2, +w2, -h2, +h2, nr, fr );

		// Reset matrices
		glMatrixMode( GL_MODELVIEW );
		glLoadIdentity();

		glPushMatrix();

		// Store and reset viewTrans translation
		auto viewTransOrig = viewTrans.translation;

		// Zoom out slightly
		viewTrans.translation = { 0, 0, -140.0 };

		// Load modified viewTrans
		glLoadMatrix( viewTrans );

		// Restore original viewTrans translation
		viewTrans.translation = viewTransOrig;

		// Find direction of axes
		auto vtr = viewTrans.rotation;
		QVector<float> axesDots = { vtr( 2, 0 ), vtr( 2, 1 ), vtr( 2, 2 ) };

		drawAxesOverlay( { 0, 0, 0 }, 50.0, sortAxes( axesDots ), uiScale );

		glPopMatrix();

		// Restore viewport size
		glViewport( 0, 0, viewportWidth, viewportHeight );
		// Restore matrices
		glProjection();
	}

	// Restore GL state
	glPopAttrib();
	glMatrixMode( GL_MODELVIEW );
	glPopMatrix();
	glMatrixMode( GL_PROJECTION );
	glPopMatrix();

	// Check for errors
	GLenum err;
	while ( ( err = glGetError() ) != GL_NO_ERROR )
		qDebug() << tr( "glview.cpp - GL ERROR (paint): " ) << (const char *)gluErrorString( err );

	emit paintUpdate();

	// Manually handle the buffer swap
	swapBuffers();

#ifdef USE_GL_QPAINTER
	painter.end();
#endif
}


void GLView::resizeGL( [[maybe_unused]] int width, [[maybe_unused]] int height )
{
	makeCurrent();
	if ( !isValid() )
		return;
	cacheViewportSize();
	glViewport( 0, 0, viewportWidth, viewportHeight );

	glDisable(GL_FRAMEBUFFER_SRGB);
	qglClearColor(clearColor());
	update();
}

void GLView::resizeEvent( QResizeEvent * e )
{
	Q_UNUSED( e );
	// This function should never be called.
	// Moved to NifSkope::eventFilter()
}

void GLView::setFrontalLight( bool frontal )
{
	frontalLight = frontal;
	update();
}

void GLView::setBrightness( int value )
{
	if ( value > 900 ) {
		value += pow(value - 900, 1.5);
	}

	brightness = float(value) / 720.0;
	update();
}

void GLView::setAmbient( int value )
{
	ambient = float( value ) / 1440.0;
	update();
}

void GLView::setDeclination( int decl )
{
	declination = float(decl) / 4; // Divide by 4 because sliders are -720 <-> 720
	lightVisTimer->start( lightVisTimeout );
	setVisMode( Scene::VisLightPos, true );
	update();
}

void GLView::setPlanarAngle( int angle )
{
	planarAngle = float(angle) / 4; // Divide by 4 because sliders are -720 <-> 720
	lightVisTimer->start( lightVisTimeout );
	setVisMode( Scene::VisLightPos, true );
	update();
}

void GLView::setDebugMode( DebugMode mode )
{
	debugMode = mode;
}

void GLView::setVisMode( Scene::VisMode mode, bool checked )
{
	if ( checked )
		scene->visMode |= mode;
	else
		scene->visMode &= ~mode;

	update();
}

typedef void (Scene::* DrawFunc)( void );

int indexAt( /*GLuint *buffer,*/ NifModel * model, Scene * scene, QList<DrawFunc> drawFunc, int cycle, const QPoint & pos, int & furn )
{
	Q_UNUSED( model ); Q_UNUSED( cycle );
	// Color Key O(1) selection
	//	Open GL 3.0 says glRenderMode is deprecated
	//	ATI OpenGL API implementation of GL_SELECT corrupts NifSkope memory
	//
	// Create FBO for sharp edges and no shading.
	// Texturing, blending, dithering, lighting and smooth shading should be disabled.
	// The FBO can be used for the drawing operations to keep the drawing operations invisible to the user.

	GLint viewport[4];
	glGetIntegerv( GL_VIEWPORT, viewport );

	// Create new FBO with multisampling disabled
	QOpenGLFramebufferObjectFormat fboFmt;
	fboFmt.setTextureTarget( GL_TEXTURE_2D );
	fboFmt.setInternalTextureFormat( GL_RGB32F_ARB );
	fboFmt.setAttachment( QOpenGLFramebufferObject::Attachment::CombinedDepthStencil );

	QOpenGLFramebufferObject fbo( viewport[2], viewport[3], fboFmt );
	fbo.bind();

	glEnable( GL_LIGHTING );
	glDisable( GL_MULTISAMPLE );
	glDisable( GL_MULTISAMPLE_ARB );
	glDisable( GL_LINE_SMOOTH );
	glDisable( GL_POINT_SMOOTH );
	glDisable( GL_POLYGON_SMOOTH );
	glDisable( GL_TEXTURE_1D );
	glDisable( GL_TEXTURE_2D );
	glDisable( GL_TEXTURE_3D );
	glDisable( GL_BLEND );
	glDisable( GL_DITHER );
	glDisable( GL_FOG );
	glDisable( GL_LIGHTING );
	glShadeModel( GL_FLAT );
	glEnable( GL_DEPTH_TEST );
	glDepthFunc( GL_LEQUAL );
	glClearColor( 0, 0, 0, 1 );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

	// Rasterize the scene
	Node::SELECTING = 1;
	for ( DrawFunc df : drawFunc ) {
		(scene->*df)();
	}
	Node::SELECTING = 0;

	fbo.release();

	QImage img( fbo.toImage() );
	QColor pixel = img.pixel( pos );

#ifndef QT_NO_DEBUG
	img.save( "fbo.png" );
#endif

	// Encode RGB to Int
	int a = 0;
	a |= pixel.red()   << 0;
	a |= pixel.green() << 8;
	a |= pixel.blue()  << 16;

	// Decode:
	// R = (id & 0x000000FF) >> 0
	// G = (id & 0x0000FF00) >> 8
	// B = (id & 0x00FF0000) >> 16

	int choose = COLORKEY2ID( a );

	// Pick BSFurnitureMarker
	if ( choose > 0 ) {
		auto furnBlock = model->getBlockIndex( model->index( 3, 0, model->getBlockIndex( choose & 0x0ffff ) ), "BSFurnitureMarker" );

		if ( furnBlock.isValid() ) {
			furn = choose >> 16;
			choose &= 0x0ffff;
		}
	}

	//qDebug() << "Key:" << a << " R" << pixel.red() << " G" << pixel.green() << " B" << pixel.blue();
	return choose;
}

QModelIndex GLView::indexAt( const QPoint & pos, int cycle )
{
	if ( !(model && isVisible() && height()) )
		return QModelIndex();

	makeCurrent();
	if ( !isValid() )
		return {};

	glPushAttrib( GL_ALL_ATTRIB_BITS );
	glMatrixMode( GL_PROJECTION );
	glPushMatrix();
	glMatrixMode( GL_MODELVIEW );
	glPushMatrix();

	glViewport( 0, 0, width(), height() );
	glProjection( pos.x(), pos.y() );

	QList<DrawFunc> df;

	if ( scene->hasOption(Scene::ShowCollision) )
		df << &Scene::drawHavok;

	if ( scene->hasOption(Scene::ShowNodes) )
		df << &Scene::drawNodes;

	if ( scene->hasOption(Scene::ShowMarkers) )
		df << &Scene::drawFurn;

	df << &Scene::drawShapes;

	int choose = -1, furn = -1;
	choose = ::indexAt( model, scene, df, cycle, pos, /*out*/ furn );

	glPopAttrib();
	glMatrixMode( GL_MODELVIEW );
	glPopMatrix();
	glMatrixMode( GL_PROJECTION );
	glPopMatrix();

	QModelIndex chooseIndex;

	if ( scene->isSelModeVertex() ) {
		// Vertex
		int block = choose >> 16;
		int vert = choose - (block << 16);

		auto shape = scene->shapes.value( block );
		if ( shape )
			chooseIndex = shape->vertexAt( vert );
	} else if ( choose != -1 ) {
		// Block Index
		chooseIndex = model->getBlockIndex( choose );

		if ( furn != -1 ) {
			// Furniture Row @ Block Index
			chooseIndex = model->index( furn, 0, model->index( 3, 0, chooseIndex ) );
		}			
	}

	return chooseIndex;
}

void GLView::center()
{
	doCenter = true;
	update();
}

void GLView::moveBy( float x, float y, float z )
{
	Pos += Matrix::euler( deg2rad(Rot[0]), deg2rad(Rot[1]), deg2rad(Rot[2]) ).inverted() * Vector3( x, y, z );
	update();
	resetViewMode();
}

void GLView::rotateBy( float x, float y, float z )
{
	setRotation( Rot[0] + x, Rot[1] + y, Rot[2] + z );
	resetViewMode();
}

void GLView::setCenter()
{
	Node * node = scene->getNode( model, scene->currentBlock );

	if ( node ) {
		// Center on selected node
		BoundSphere bs = node->bounds();

		setPosition( -bs.center );

		if ( bs.radius > 0 ) {
			setDistance( bs.radius * 1.2 );
		}
	} else {
		// Center on entire mesh
		BoundSphere bs = scene->bounds();

		if ( bs.radius < 1 * scale() )
			bs.radius = 1024.0 * scale();

		setDistance( bs.radius * 1.2 );
		setZoom( 1.0 );

		setPosition( -bs.center );
	}
}

void GLView::setDistance( float x )
{
	Dist = x;
	update();
}

void GLView::setPosition( float x, float y, float z )
{
	Pos = { x, y, z };
	update();
}

void GLView::setPosition( const Vector3 & v )
{
	Pos = v;
	update();
}

void GLView::setProjection( bool isPersp )
{
	perspectiveMode = isPersp;
	update();
}

void GLView::setRotation( float x, float y, float z )
{
	Rot[0] = normDeg( x );
	Rot[1] = normDeg( y );
	Rot[2] = normDeg( z );
	update();
}

void GLView::setZoom( float z )
{
	Zoom = z;

	if (Zoom < ZOOM_MIN)
		Zoom = ZOOM_MIN;

	if (Zoom > ZOOM_MAX)
		Zoom = ZOOM_MAX;

	update();
}


void GLView::flipView()
{
	switch ( view ) {
	case ViewTop:
		setViewMode( ViewBottom );
		break;
	case ViewBottom:
		setViewMode( ViewTop );
		break;
	case ViewLeft:
		setViewMode( ViewRight );
		break;
	case ViewRight:
		setViewMode( ViewLeft );
		break;
	case ViewFront:
		setViewMode( ViewBack );
		break;
	case ViewBack:
		setViewMode( ViewFront );
		break;
	default:
		setRotation( 180.0f - Rot[0], Rot[1], Rot[2] + 180.0f );
		resetViewMode();
		break;
	}
}

void GLView::setViewMode( GLView::ViewState newView )
{
	if ( view == newView )
		return;

	auto oldView = view;
	view = newView;

	auto onDirViewModeChange = [this, oldView]( float rotX, float rotY, float rotZ ) {
		setRotation( rotX, rotY, rotZ );
		if ( oldView != ViewBottom && oldView != ViewTop && oldView != ViewBack && oldView != ViewFront && oldView != ViewRight && oldView != ViewLeft )
			center();
	};

	switch ( view ) {
	case ViewBottom:
		onDirViewModeChange( 180, 0, 0 ); // Bottom
		break;
	case ViewTop:
		onDirViewModeChange( 0, 0, 0 ); // Top
		break;
	case ViewBack:
		onDirViewModeChange( -90, 0, 0 ); // Back
		break;
	case ViewFront:
		onDirViewModeChange( -90, 0, 180 ); // Front
		break;
	case ViewRight:
		onDirViewModeChange( -90, 0, 90 ); // Right
		break;
	case ViewLeft:
		onDirViewModeChange( -90, 0, -90 ); // Left
		break;
	case ViewWalk:
		center();
		break;
	case ViewUser:
		loadUserView();
		break;
	}

	emit viewModeChanged();
}

void GLView::resetViewMode()
{
	if ( view != ViewDefault && view != ViewWalk )
		setViewMode( ViewDefault );
}

void GLView::flush()
{
	if ( textures )
		textures->flush();
}


/*
 *  NifModel
 */

void GLView::setNif( NifModel * nif )
{
	if ( model ) {
		// disconnect( model ) may not work with new Qt5 syntax...
		// it says the calls need to remain symmetric to the connect() ones.
		// Otherwise, use QMetaObject::Connection
		disconnect( model );
	}

	model = nif;

	if ( model ) {
		connect( model, &NifModel::dataChanged, this, &GLView::dataChanged );
		connect( model, &NifModel::linksChanged, this, &GLView::modelLinked );
		connect( model, &NifModel::modelReset, this, &GLView::modelChanged );
		connect( model, &NifModel::destroyed, this, &GLView::modelDestroyed );
	}

	doCompile = true;
}

void GLView::setCurrentIndex( const QModelIndex & index )
{
	if ( !( model && index.model() == model ) )
		return;

	scene->currentBlock = model->getBlockIndex( index );
	scene->currentIndex = index.sibling( index.row(), 0 );

	update();
}

QModelIndex parent( QModelIndex ix, QModelIndex xi )
{
	ix = ix.sibling( ix.row(), 0 );
	xi = xi.sibling( xi.row(), 0 );

	while ( ix.isValid() ) {
		QModelIndex x = xi;

		while ( x.isValid() ) {
			if ( ix == x )
				return ix;

			x = x.parent();
		}

		ix = ix.parent();
	}

	return QModelIndex();
}

void GLView::dataChanged( const QModelIndex & idx, const QModelIndex & xdi )
{
	if ( doCompile )
		return;

	QModelIndex ix = idx;

	if ( idx == xdi ) {
		if ( idx.column() != 0 )
			ix = idx.sibling( idx.row(), 0 );
	} else {
		ix = ::parent( idx, xdi );
	}

	if ( ix.isValid() ) {
		scene->update( model, idx );
		update();
	} else {
		modelChanged();
	}
}

void GLView::modelChanged()
{
	if ( doCompile )
		return;

	doCompile = true;
	doTimerReset = true;
	//doCenter  = true;
	update();
}

void GLView::modelLinked()
{
	if ( doCompile )
		return;

	doCompile = true; //scene->update( model, QModelIndex() );
	update();
}

void GLView::modelDestroyed()
{
	setNif( nullptr );
}


/*
 * UI
 */

void GLView::setSceneTime( float t )
{
	time = t;
	update();
	emit sceneTimeChanged( time, scene->timeMin(), scene->timeMax() );
}

void GLView::setSceneSequence( const QString & seqname )
{
	// Update UI
	QAction * action = qobject_cast<QAction *>(sender());
	if ( !action ) {
		// Called from self and not UI
		emit sequenceChanged( seqname );
	}
	
	scene->setSequence( seqname );
	time = scene->timeMin();
	emit sceneTimeChanged( time, scene->timeMin(), scene->timeMax() );
	update();
}

// TODO: Multiple user views, ala Recent Files
void GLView::saveUserView()
{
	QSettings settings;
	settings.beginGroup( "GLView" );
	settings.beginGroup( "User View" );
	settings.setValue( "RotX", Rot[0] );
	settings.setValue( "RotY", Rot[1] );
	settings.setValue( "RotZ", Rot[2] );
	settings.setValue( "PosX", Pos[0] );
	settings.setValue( "PosY", Pos[1] );
	settings.setValue( "PosZ", Pos[2] );
	settings.setValue( "Dist", Dist );
	settings.endGroup();
	settings.endGroup();
}

void GLView::loadUserView()
{
	QSettings settings;
	settings.beginGroup( "GLView" );
	settings.beginGroup( "User View" );
	setRotation( settings.value( "RotX" ).toDouble(), settings.value( "RotY" ).toDouble(), settings.value( "RotZ" ).toDouble() );
	setPosition( settings.value( "PosX" ).toDouble(), settings.value( "PosY" ).toDouble(), settings.value( "PosZ" ).toDouble() );
	setDistance( settings.value( "Dist" ).toDouble() );
	settings.endGroup();
	settings.endGroup();
}

void GLView::advanceGears()
{
	QTime t  = QTime::currentTime();
	float dT = lastTime.msecsTo( t ) / 1000.0;
	dT = (dT < 0) ? 0 : ((dT > 1.0) ? 1.0 : dT);

	lastTime = t;

	if ( !isVisible() )
		return;

	if ( ( animState & AnimEnabled ) && ( animState & AnimPlay )
		&& scene->timeMin() != scene->timeMax() )
	{
		time += dT;

		if ( time > scene->timeMax() ) {
			if ( ( animState & AnimSwitch ) && !scene->animGroups.isEmpty() ) {
				int ix = scene->animGroups.indexOf( scene->animGroup );
	
				if ( ++ix >= scene->animGroups.count() )
					ix -= scene->animGroups.count();
	
				setSceneSequence( scene->animGroups.value( ix ) );
			} else if ( animState & AnimLoop ) {
				time = scene->timeMin();
			} else {
				// Animation has completed and is not looping
				//	or cycling through animations.
				// Reset time and state and then inform UI it has stopped.
				time = scene->timeMin();
				animState &= ~AnimPlay;
				emit sequenceStopped();
			}
		} else {
			// Animation is not done yet
		}

		emit sceneTimeChanged( time, scene->timeMin(), scene->timeMax() );
		update();
	}

	// TODO: Some kind of input class for choosing the appropriate
	// keys based on user preferences of what app they would like to
	// emulate for the control scheme
	// Rotation
	if ( kbd[ Qt::Key_Up ] )    rotateBy( -cfg.rotSpd * dT, 0, 0 );
	if ( kbd[ Qt::Key_Down ] )  rotateBy( +cfg.rotSpd * dT, 0, 0 );
	if ( kbd[ Qt::Key_Left ] )  rotateBy( 0, 0, -cfg.rotSpd * dT );
	if ( kbd[ Qt::Key_Right ] ) rotateBy( 0, 0, +cfg.rotSpd * dT );

	// Fix movement speed for Starfield scale
	dT *= scale();
	// Movement
	if ( kbd[ Qt::Key_A ] ) moveBy( +cfg.moveSpd * dT, 0, 0 );
	if ( kbd[ Qt::Key_D ] ) moveBy( -cfg.moveSpd * dT, 0, 0 );
	if ( kbd[ Qt::Key_W ] ) moveBy( 0, 0, +cfg.moveSpd * dT );
	if ( kbd[ Qt::Key_S ] ) moveBy( 0, 0, -cfg.moveSpd * dT );
	if ( kbd[ Qt::Key_Q ] ) moveBy( 0, +cfg.moveSpd * dT, 0 );
	if ( kbd[ Qt::Key_E ] ) moveBy( 0, -cfg.moveSpd * dT, 0 );

	// Zoom
	//if ( kbd[ Qt::Key_R ] ) setDistance( Dist / ZOOM_QE_KEY_MULT );
	//if ( kbd[ Qt::Key_F ] ) setDistance( Dist * ZOOM_QE_KEY_MULT );

	// Focal Length
	if ( kbd[ Qt::Key_PageUp ] )   setZoom( Zoom * ZOOM_PAGE_KEY_MULT );
	if ( kbd[ Qt::Key_PageDown ] ) setZoom( Zoom / ZOOM_PAGE_KEY_MULT );

	if ( mouseMov[0] != 0 || mouseMov[1] != 0 || mouseMov[2] != 0 ) {
		moveBy( mouseMov[0], mouseMov[1], mouseMov[2] );
		mouseMov = Vector3();
	}

	if ( mouseRot[0] != 0 || mouseRot[1] != 0 || mouseRot[2] != 0 ) {
		rotateBy( mouseRot[0], mouseRot[1], mouseRot[2] );
		mouseRot = Vector3();
	}
}

void GLView::resetAnimation()
{
	if ( animState & AnimPlay ) {
		time = scene->timeMin();
		animState &= ~AnimPlay;
		emit sequenceStopped();
		emit sceneTimeChanged( time, scene->timeMin(), scene->timeMax() );
		update();
	}
}

ScreenshotDialog::ScreenshotDialog( GLView * parent )
	: ToolDialog( parent, tr("Save Screenshot"), ToolDialog::HResize, 500 ), // parent
	view( parent )
{
	setSettingsFolder( "Screenshot" );

	appScreenshotsPath = QFileInfo( QApplication::applicationDirPath() + "/screenshots" ).absoluteFilePath();
	const QString & modelDirPath = modelFolder();
	bool hasModelFile = !modelDirPath.isEmpty();

	QVBoxLayout * mainLayout = addVBoxLayout( this );

	// Image path
	QString imgName;
	if ( hasModelFile ) {
		imgName += view->model->getFilename();
		if ( !imgName.isEmpty() )
			imgName += "-";
	}
	imgName += QDateTime::currentDateTime().toString( "yyyy-MM-dd-HHmmss" ) + "." + settingsStrValue( "Format", "jpg" );

	pathWidget = new FileSelector( FileSelector::SaveFile, "...", QBoxLayout::LeftToRight );
	pathWidget->setFilter( { "Images (*.jpg *.png *.webp *.bmp)", "JPEG (*.jpg)", "PNG (*.png)", "WebP (*.webp)", "BMP (*.bmp)" } );
	pathWidget->setFile( imgName );
	switchToDirectory( ( hasModelFile && settingsIntValue( "ModelDirectory", 0 ) ) ? modelDirPath : appScreenshotsPath );
	connect( pathWidget, &FileSelector::sigEdited, this, &ScreenshotDialog::onPathEdit );
	connect( pathWidget, &FileSelector::sigActivated, this, &ScreenshotDialog::onPathEdit );
	mainLayout->addWidget( pathWidget );

	// Directory selectors
	QHBoxLayout * dirSelLayout = addHBoxLayout( mainLayout );

	QPushButton * btnAppDir = addPushButton( dirSelLayout, tr("NifSkope Directory") );
	lockPushButtonSize( btnAppDir );
	btnAppDir->setToolTip( tr("Switch to NifSkope screenshots directory") );
	connect( btnAppDir, &QPushButton::clicked, this, &ScreenshotDialog::onAppDirClicked );

	QPushButton * btnModelDir = addPushButton( dirSelLayout, tr("NIF Directory") );
	lockPushButtonSize( btnModelDir );
	btnModelDir->setToolTip( tr("Switch to the current NIF file directory") );
	btnModelDir->setEnabled( hasModelFile );
	connect( btnModelDir, &QPushButton::clicked, this, &ScreenshotDialog::onNifDirClicked );

	addStretch( dirSelLayout, 1 );

	// JPEG/WebP Quality
	qualityLabel = addLabel( dirSelLayout, tr("Quality:"), true );
	qualityBox = addSpinBox( dirSelLayout, 0, 100, settingsIntValue( "Quality", 95 ) );
	updateQualityUI( imagePathInfo().suffix() );

	// Image scale
	QHBoxLayout * imageScaleLayout = addHBoxLayout();
	imageScaleGroup = beginRadioGroup();

	GLint dims;	
	glGetIntegerv( GL_MAX_VIEWPORT_DIMS, &dims ); // Get max viewport size for platform
	int maxSize = dims;

	for ( int sz = 1; sz <= 8; sz *= 2 ) {
		QRadioButton * btn = addRadioButton( imageScaleLayout, QString::number( sz ) + "x", sz );
		if ( sz == 1 ) {
			btn->setChecked( true );
		} else { // sz > 1
			btn->setDisabled( ( view->viewportWidth * sz ) > maxSize || ( view->viewportHeight * sz ) > maxSize );
		}
	}
	addLabel( imageScaleLayout, tr("<b>Caution:</b><br/> 4x and 8x may be memory intensive.") );
	addStretch( imageScaleLayout, 1 );

	addGroupBox( mainLayout, tr("Image Size"), imageScaleLayout );

	// Main buttons
	beginMainButtonLayout( mainLayout );

	QPushButton * btnSave = addMainButton( tr("Save"), true );
	connect( btnSave, &QPushButton::clicked, this, &ScreenshotDialog::onSaveClicked );
	addCloseButton( tr("Cancel") );
}

const QString & ScreenshotDialog::modelFolder() const
{
	return view->model->getFolder();
}

QFileInfo ScreenshotDialog::imagePathInfo() const
{
	if ( pathWidget )
		return QFileInfo( pathWidget->file() );
	return QFileInfo();
}

static bool isJpegExtension( const QString & extension )
{
	return extension.compare( QStringLiteral("jpg"), Qt::CaseInsensitive ) == 0 || extension.compare( QStringLiteral("jpeg"), Qt::CaseInsensitive ) == 0;
}

static bool isWebpExtension( const QString & extension )
{
	return extension.compare( QStringLiteral("webp"), Qt::CaseInsensitive ) == 0;
}

void ScreenshotDialog::updateQualityUI( const QString & extension )
{
	bool qualityEnabled = isJpegExtension( extension ) || isWebpExtension( extension );
	if ( qualityLabel )
		qualityLabel->setEnabled( qualityEnabled );
	if ( qualityBox )
		qualityBox->setEnabled( qualityEnabled );
}

void ScreenshotDialog::onPathEdit()
{
	updateQualityUI( imagePathInfo().suffix() );
}

void ScreenshotDialog::onAppDirClicked()
{
	switchToDirectory( appScreenshotsPath );
}

void ScreenshotDialog::onNifDirClicked()
{
	const QString & modelDirPath = modelFolder();
	if ( !modelDirPath.isEmpty() )
		switchToDirectory( modelDirPath );
}

void ScreenshotDialog::switchToDirectory( const QString & dirPath )
{
	QString newPath = QDir( dirPath ).filePath( imagePathInfo().fileName() );
	pathWidget->setText( QDir::toNativeSeparators( newPath ) );
}

void ScreenshotDialog::onSaveClicked()
{
	QFileInfo pathInfo = imagePathInfo();
	QString outPath = QDir::toNativeSeparators( pathInfo.absoluteFilePath() );
	QString outExt = pathInfo.suffix();
	QDir outDir( pathInfo.absolutePath() );
	const QString & modelDirPath = modelFolder();
	int quality = qualityBox->value();
	int imageScale = std::max( imageScaleGroup->checkedId(), 1 );

	setSettingsStrValue( "Format", outExt );
	setSettingsIntValue( "Quality", quality );
	setSettingsIntValue( "ModelDirectory", outDir == QDir( modelDirPath ) );

	QDir appScreenshotsDir( appScreenshotsPath );
	if ( outDir == appScreenshotsDir && !appScreenshotsDir.exists() ) {
		if ( !QDir().mkdir( appScreenshotsPath ) ) {
			QFileInfo appScreenshotsInfo( appScreenshotsPath );
			Message::critical( this, tr("Could not create \"%1\" folder in \"%2\".").arg( appScreenshotsInfo.fileName(), appScreenshotsInfo.absolutePath() ) );
			return;
		}
	}

	if ( imageScale > 1 ) // Image scales 2x or greater can take a significant amount of time to save...
		setCursor( Qt::WaitCursor );

	QImage img = view->captureScreenshot( imageScale );

	QImageWriter writer( outPath );

	// Set Compression for formats that can use it
	writer.setCompression( 1 );

	// Handle JPEG/WebP Quality exclusively
	//	PNG will not use compression if Quality is set
	if ( isJpegExtension( outExt ) ) {
		writer.setFormat( "jpg" );
		writer.setQuality( quality );
	} else if ( isWebpExtension( outExt ) ) {
		writer.setFormat( "webp" );
		writer.setQuality( quality );
	}

	if ( writer.write( img ) ) {
		close();
	} else {
		Message::critical( this, tr("Could not save \"%1\":\n\n").arg( outPath ) + writer.errorString() );
	} 

	if ( imageScale > 1 )
		setCursor( Qt::ArrowCursor );
}

void GLView::saveImage()
{
	auto dlg = new ScreenshotDialog( this );
	dlg->open( true );
}

QImage GLView::captureScreenshot( int imageScale )
{
	// Supersampling
	int oldw = width();
	int oldh = height();

	// Resize viewport for supersampling
	if ( imageScale > 1 ) {
		int neww = oldw * imageScale;
		int newh = oldh * imageScale;

		globalScale = imageScale;
		resize( neww, newh );
		resizeGL( neww, newh );
	}

	QOpenGLFramebufferObjectFormat fboFmt;
	fboFmt.setTextureTarget( GL_TEXTURE_2D );
	fboFmt.setInternalTextureFormat( GL_RGB );
	fboFmt.setMipmap( false );
	fboFmt.setAttachment( QOpenGLFramebufferObject::Attachment::Depth );
	fboFmt.setSamples( 16 );

	QOpenGLFramebufferObject fbo( viewportWidth, viewportHeight, fboFmt );
	fbo.bind();

	updateGL();

	fbo.release();

	auto img = fbo.toImage();

	// Return viewport to the original size
	if ( imageScale > 1 ) {
		globalScale = 1;
		resize( oldw, oldh );
		resizeGL( oldw, oldh );
		updateGL();
	}

	return img;
}


/* 
 * QWidget Event Handlers 
 */

void GLView::dragEnterEvent( QDragEnterEvent * e )
{
	auto md = e->mimeData();
	if ( md && md->hasUrls() && md->urls().count() == 1 ) {
		QUrl url = md->urls().first();

		if ( url.scheme() == "file" ) {
			QString fn = url.toLocalFile();

			if ( textures->canLoad( fn ) ) {
				fnDragTex = textures->stripPath( fn, model->getFolder() );
				e->accept();
				return;
			}
		}
	}

	e->ignore();
}

void GLView::dragLeaveEvent( QDragLeaveEvent * e )
{
	Q_UNUSED( e );

	if ( iDragTarget.isValid() ) {
		model->set<QString>( iDragTarget, fnDragTexOrg );
		iDragTarget = QModelIndex();
		fnDragTex = fnDragTexOrg = QString();
	}
}

void GLView::dragMoveEvent( QDragMoveEvent * e )
{
	if ( iDragTarget.isValid() ) {
		model->set<QString>( iDragTarget, fnDragTexOrg );
		iDragTarget  = QModelIndex();
		fnDragTexOrg = QString();
	}

	QModelIndex iObj = model->getBlockIndex( indexAt( e->pos() ), "NiAVObject" );

	if ( iObj.isValid() ) {
		for ( const auto l : model->getChildLinks( model->getBlockNumber( iObj ) ) ) {
			QModelIndex iTxt = model->getBlockIndex( l, "NiTexturingProperty" );

			if ( iTxt.isValid() ) {
				QModelIndex iSrc = model->getBlockIndex( model->getLink( iTxt, "Base Texture/Source" ), "NiSourceTexture" );

				if ( iSrc.isValid() ) {
					iDragTarget = model->getIndex( iSrc, "File Name" );

					if ( iDragTarget.isValid() ) {
						fnDragTexOrg = model->get<QString>( iDragTarget );
						model->set<QString>( iDragTarget, fnDragTex );
						e->accept();
						return;
					}
				}
			}
		}
	}

	e->ignore();
}

void GLView::dropEvent( QDropEvent * e )
{
	iDragTarget = QModelIndex();
	fnDragTex = fnDragTexOrg = QString();
	e->accept();
}

void GLView::focusOutEvent( QFocusEvent * )
{
	kbd.clear();
}

void GLView::keyPressEvent( QKeyEvent * event )
{
	switch ( event->key() ) {
	case Qt::Key_Up:
	case Qt::Key_Down:
	case Qt::Key_Left:
	case Qt::Key_Right:
	case Qt::Key_PageUp:
	case Qt::Key_PageDown:
	case Qt::Key_A:
	case Qt::Key_D:
	case Qt::Key_W:
	case Qt::Key_S:
	//case Qt::Key_R:
	//case Qt::Key_F:
	case Qt::Key_Q:
	case Qt::Key_E:
	case Qt::Key_Space:
		kbd[event->key()] = true;
		break;
	case Qt::Key_Escape:
		doCompile = true;

		if ( view == ViewWalk )
			doCenter = true;

		update();
		break;
	default:
		event->ignore();
		break;
	}
}

void GLView::keyReleaseEvent( QKeyEvent * event )
{
	switch ( event->key() ) {
	case Qt::Key_Up:
	case Qt::Key_Down:
	case Qt::Key_Left:
	case Qt::Key_Right:
	case Qt::Key_PageUp:
	case Qt::Key_PageDown:
	case Qt::Key_A:
	case Qt::Key_D:
	case Qt::Key_W:
	case Qt::Key_S:
	//case Qt::Key_R:
	//case Qt::Key_F:
	case Qt::Key_Q:
	case Qt::Key_E:
	case Qt::Key_Space:
		kbd[event->key()] = false;
		break;
	default:
		event->ignore();
		break;
	}
}

void GLView::mouseDoubleClickEvent( QMouseEvent * )
{
	/*
	doCompile = true;
	if ( ! aViewWalk->isChecked() )
	doCenter = true;
	update();
	*/
}

void GLView::mouseMoveEvent( QMouseEvent * event )
{
	int dx = event->x() - lastPos.x();
	int dy = event->y() - lastPos.y();

	if ( event->buttons() & Qt::LeftButton && !kbd[Qt::Key_Space] ) {
		mouseRot += Vector3( dy * .5, 0, dx * .5 );
	} else if ( (event->buttons() & Qt::MidButton) || (event->buttons() & Qt::LeftButton && kbd[Qt::Key_Space]) ) {
		float d = axis / (qMax( width(), height() ) + 1);
		mouseMov += Vector3( dx * d, -dy * d, 0 );
	} else if ( event->buttons() & Qt::RightButton ) {
		setDistance( Dist - (dx + dy) * (axis / (qMax( width(), height() ) + 1)) );
	}

	lastPos = event->pos();
}

void GLView::mousePressEvent( QMouseEvent * event )
{
	if ( event->button() == Qt::ForwardButton || event->button() == Qt::BackButton ) {
		event->ignore();
		return;
	}

	lastPos = event->pos();

	if ( (pressPos - event->pos()).manhattanLength() <= 3 )
		cycleSelect++;
	else
		cycleSelect = 0;

	pressPos = event->pos();
}

void GLView::mouseReleaseEvent( QMouseEvent * event )
{
	if ( !(model && (pressPos - event->pos()).manhattanLength() <= 3) )
		return;

	if ( event->button() == Qt::ForwardButton || event->button() == Qt::BackButton || event->button() == Qt::MiddleButton ) {
		event->ignore();
		return;
	}

	auto mods = event->modifiers();

	if ( !(mods & Qt::AltModifier) ) {
		QModelIndex idx = indexAt( event->pos(), cycleSelect );
		scene->currentBlock = model->getBlockIndex( idx );
		scene->currentIndex = idx.sibling( idx.row(), 0 );

		if ( idx.isValid() ) {
			emit clicked( QModelIndex() ); // HACK: To get Block Details to update
			emit clicked( idx );
		}

	} else {
		// Color Picker / Eyedrop tool
		QOpenGLFramebufferObjectFormat fboFmt;
		fboFmt.setTextureTarget( GL_TEXTURE_2D );
		fboFmt.setInternalTextureFormat( GL_RGB );
		fboFmt.setMipmap( false );
		fboFmt.setAttachment( QOpenGLFramebufferObject::Attachment::Depth );

		QOpenGLFramebufferObject fbo( width(), height(), fboFmt );
		fbo.bind();

		update();
		updateGL();

		fbo.release();

		QImage * img = new QImage( fbo.toImage() );

		auto what = img->pixel( event->pos() );

		qglClearColor( QColor( what ) );
		// qDebug() << QColor( what );

		delete img;
	}

	update();
}

void GLView::wheelEvent( QWheelEvent * event )
{
	if ( view == ViewWalk )
		mouseMov += Vector3( 0, 0, ((double) event->delta()) / 4.0 ) * scale();
	else
	{
		if (event->delta() < 0)
			setDistance( Dist / ZOOM_MOUSE_WHEEL_MULT );
		else
			setDistance( Dist * ZOOM_MOUSE_WHEEL_MULT );
	}
}

void GLView::cacheViewportSize()
{
	auto vportSize = UIUtils::widgetRealSize( this );

	viewportWidth  = std::max( vportSize.width(), 0 );
	viewportHeight = std::max( vportSize.height(), 0 );
	aspectWidth    = viewportWidth;
	aspectHeight   = viewportHeight > 0 ? viewportHeight : 1;
	uiScale        = UIUtils::widgetUIScaleFactor( this ) * globalScale;
	axesSize       = std::min( qRound( uiScale * 125 ), std::min( viewportWidth / 10, viewportHeight ) );
}

void GLGraphicsView::setupViewport( QWidget * viewport )
{
	GLView * glWidget = qobject_cast<GLView *>(viewport);
	if ( glWidget ) {
		//glWidget->installEventFilter( this );
	}

	QGraphicsView::setupViewport( viewport );
}

bool GLGraphicsView::eventFilter( QObject * o, QEvent * e )
{
	//GLView * glWidget = qobject_cast<GLView *>(o);
	//if ( glWidget ) {
	//
	//}

	return QGraphicsView::eventFilter( o, e );
}

//void GLGraphicsView::paintEvent( QPaintEvent * e )
//{
//	GLView * glWidget = qobject_cast<GLView *>(viewport());
//	if ( glWidget ) {
//	//	glWidget->paintEvent( e );
//	}
//
//	QGraphicsView::paintEvent( e );
//}

void GLGraphicsView::drawForeground( QPainter * painter, const QRectF & rect )
{
	QGraphicsView::drawForeground( painter, rect );
}

void GLGraphicsView::drawBackground( QPainter * painter, const QRectF & rect )
{
	Q_UNUSED( painter ); Q_UNUSED( rect );

	GLView * glWidget = qobject_cast<GLView *>(viewport());
	if ( glWidget ) {
		glWidget->updateGL();
	}

	//QGraphicsView::drawBackground( painter, rect );
}

void GLGraphicsView::dragEnterEvent( QDragEnterEvent * e )
{
	// Intercept NIF files
	if ( e->mimeData()->hasUrls() ) {
		QList<QUrl> urls = e->mimeData()->urls();
		for ( auto url : urls ) {
			if ( url.scheme() == "file" ) {
				QString fn = url.toLocalFile();
				QFileInfo finfo( fn );
				if ( finfo.exists() && NifSkope::fileExtensions().contains( finfo.suffix(), Qt::CaseInsensitive ) ) {
					draggedNifs << finfo.absoluteFilePath();
				}
			}
		}

		if ( !draggedNifs.isEmpty() ) {
			e->accept();
			return;
		}
	}

	// Pass event on to viewport for any texture drag/drops
	GLView * glWidget = qobject_cast<GLView *>(viewport());
	if ( glWidget ) {
		glWidget->dragEnterEvent( e );
	}
}
void GLGraphicsView::dragLeaveEvent( QDragLeaveEvent * e )
{
	if ( !draggedNifs.isEmpty() ) {
		draggedNifs.clear();
		e->ignore();
		return;
	}

	// Pass event on to viewport for any texture drag/drops
	GLView * glWidget = qobject_cast<GLView *>(viewport());
	if ( glWidget ) {
		glWidget->dragLeaveEvent( e );
	}
}
void GLGraphicsView::dragMoveEvent( QDragMoveEvent * e )
{
	if ( !draggedNifs.isEmpty() ) {
		e->accept();
		return;
	}

	// Pass event on to viewport for any texture drag/drops
	GLView * glWidget = qobject_cast<GLView *>(viewport());
	if ( glWidget ) {
		glWidget->dragMoveEvent( e );
	}
}
void GLGraphicsView::dropEvent( QDropEvent * e )
{
	if ( !draggedNifs.isEmpty() ) {
		auto ns = qobject_cast<NifSkope *>(parentWidget());
		if ( ns ) {
			ns->openFiles( draggedNifs );
		}

		draggedNifs.clear();
		e->accept();
		return;
	}

	// Pass event on to viewport for any texture drag/drops
	GLView * glWidget = qobject_cast<GLView *>(viewport());
	if ( glWidget ) {
		glWidget->dropEvent( e );
	}
}
void GLGraphicsView::focusOutEvent( QFocusEvent * e )
{
	GLView * glWidget = qobject_cast<GLView *>(viewport());
	if ( glWidget ) {
		glWidget->focusOutEvent( e );
	}
}
void GLGraphicsView::keyPressEvent( QKeyEvent * e )
{
	GLView * glWidget = qobject_cast<GLView *>(viewport());
	if ( glWidget ) {
		glWidget->keyPressEvent( e );
	}
}
void GLGraphicsView::keyReleaseEvent( QKeyEvent * e )
{
	GLView * glWidget = qobject_cast<GLView *>(viewport());
	if ( glWidget ) {
		glWidget->keyReleaseEvent( e );
	}
}
void GLGraphicsView::mouseDoubleClickEvent( QMouseEvent * e )
{
	GLView * glWidget = qobject_cast<GLView *>(viewport());
	if ( glWidget ) {
		glWidget->mouseDoubleClickEvent( e );
	}
}
void GLGraphicsView::mouseMoveEvent( QMouseEvent * e )
{
	GLView * glWidget = qobject_cast<GLView *>(viewport());
	if ( glWidget ) {
		glWidget->mouseMoveEvent( e );
	}
}
void GLGraphicsView::mousePressEvent( QMouseEvent * e )
{
	GLView * glWidget = qobject_cast<GLView *>(viewport());
	if ( glWidget ) {
		glWidget->mousePressEvent( e );
	}
}
void GLGraphicsView::mouseReleaseEvent( QMouseEvent * e )
{
	GLView * glWidget = qobject_cast<GLView *>(viewport());
	if ( glWidget ) {
		glWidget->mouseReleaseEvent( e );
	}
}
void GLGraphicsView::wheelEvent( QWheelEvent * e )
{
	GLView * glWidget = qobject_cast<GLView *>(viewport());
	if ( glWidget ) {
		glWidget->wheelEvent( e );
	}
}
