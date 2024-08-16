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

#include "glscene.h"

#include "gl/renderer.h"
#include "gl/gltex.h"
#include "gl/glcontroller.h"
#include "gl/glmesh.h"
#include "gl/bsshape.h"
#include "gl/BSMesh.h"
#include "gl/glparticles.h"
#include "gl/gltex.h"
#include "model/nifmodel.h"

#include <QAction>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSettings>


//! \file glscene.cpp %Scene management

Scene::Scene( TexCache * texcache, QOpenGLContext * context, QOpenGLFunctions * functions, QObject * parent ) :
	QObject( parent )
{
	setGame( Game::OTHER );

	renderer = new Renderer( context, functions );

	currentBlock = currentIndex = QModelIndex();
	animate = true;

	time = 0.0;
	sceneBoundsValid = timeBoundsValid = false;

	textures = texcache;

	options = ( DoLighting | DoTexturing | DoMultisampling | DoBlending | DoVertexColors | DoSpecular | DoGlow | DoCubeMapping );

	visMode = VisNone;

	selMode = SelObject;

	// Startup Defaults

	QSettings settings;
	settings.beginGroup( "Settings/Render/General/Startup Defaults" );

	if ( settings.value( "Show Axes", true ).toBool() )
		options |= ShowAxes;
	if ( settings.value( "Show Grid", true ).toBool() )
		options |= ShowGrid;
	if ( settings.value( "Show Collision" ).toBool() )
		options |= ShowCollision;
	if ( settings.value( "Show Constraints" ).toBool() )
		options |= ShowConstraints;
	if ( settings.value( "Show Markers" ).toBool() )
		options |= ShowMarkers;
	if ( settings.value( "Show Nodes" ).toBool() )
		options |= ShowNodes;
	if ( settings.value( "Show Hidden" ).toBool() )
		options |= ShowHidden;
	if ( settings.value( "Do Skinning", true ).toBool() )
		options |= DoSkinning;
	if ( settings.value( "Do Error Color", true ).toBool() )
		options |= DoErrorColor;

	settings.endGroup();
}

Scene::~Scene()
{
	delete renderer;
}

void Scene::setGame( Game::GameMode newGame )
{
	game = newGame;
	lodLevel = defaultLodLevel();
}

void Scene::updateShaders()
{
	renderer->updateShaders();
}

void Scene::clear( bool flushTextures )
{
	Q_UNUSED( flushTextures );
	nodes.clear();
	properties.clear();
	roots.clear();
	shapes.clear();

	animGroups.clear();
	animTags.clear();

	//if ( flushTextures )
	textures->flush();

	sceneBoundsValid = timeBoundsValid = false;

	setGame( Game::OTHER );
}

void Scene::update( const NifModel * nif, const QModelIndex & index )
{
	if ( !nif )
		return;

	if ( index.isValid() ) {
		QModelIndex block = nif->getBlockIndex( index );
		if ( !block.isValid() )
			return;

		for ( Property * prop : properties.hash() )
			prop->update( nif, block );

		for ( Node * node : nodes.list() )
			node->update( nif, block );
	} else {
		properties.validate();
		nodes.validate();

		for ( Property * p : properties.hash() )
			p->update();

		for ( Node * n : nodes.list() )
			n->update();

		roots.clear();
		for ( const auto link : nif->getRootLinks() ) {
			QModelIndex iBlock = nif->getBlockIndex( link );
			if ( iBlock.isValid() ) {
				Node * node = getNode( nif, iBlock );
				if ( node ) {
					node->makeParent( nullptr );
					roots.add( node );
				}
			}
		}
	}

	timeBoundsValid = false;
}

void Scene::updateSceneOptions( bool checked )
{
	Q_UNUSED( checked );

	QAction * action = qobject_cast<QAction *>(sender());
	if ( action ) {
		options ^= SceneOptions( action->data().toInt() );
		emit sceneUpdated();
	}
}

void Scene::updateSceneOptionsGroup( QAction * action )
{
	if ( !action )
		return;

	options ^= SceneOptions( action->data().toInt() );
	emit sceneUpdated();
}

void Scene::updateSelectMode( QAction * action )
{
	if ( !action )
		return;

	selMode = SelMode( action->data().toInt() );
	emit sceneUpdated();
}

int Scene::maxLodLevel() const
{
	return ( game == Game::STARFIELD ) ? MAX_LOD_LEVEL_STARFIELD : MAX_LOD_LEVEL_DEFAULT;
}

int Scene::defaultLodLevel() const
{
	return ( game == Game::STARFIELD ) ? 0 : 2;
}

void Scene::updateLodLevel( int newLevel )
{
	if ( newLevel < 0 || newLevel > maxLodLevel() )
		newLevel = defaultLodLevel();
	lodLevel = newLevel;
}

void Scene::make( NifModel * nif, bool flushTextures )
{
	clear( flushTextures );

	if ( !nif )
		return;

	setGame( Game::GameManager::get_game(nif->getVersionNumber(), nif->getUserVersion(), nif->getBSVersion()) );

	update( nif, QModelIndex() );

	if ( !animGroups.contains( animGroup ) ) {
		if ( animGroups.isEmpty() )
			animGroup = QString();
		else
			animGroup = animGroups.first();
	}

	setSequence( animGroup );
}

Node * Scene::getNode( const NifModel * nif, const QModelIndex & iNode )
{
	if ( !nif || !iNode.isValid() )
		return nullptr;

	return getNode( nif->field( iNode ) );
}

Node * Scene::getNode( NifFieldConst nodeBlock )
{
	if ( !nodeBlock )
		return nullptr;

	Node * node = nodes.get( nodeBlock );
	if ( node )
		return node;

	if ( !nodeBlock.isBlock() ) {
		nodeBlock.model()->reportError( tr("Scene::getNode: item '%1' is not a block.").arg( nodeBlock.repr() ) );

	} else if ( nodeBlock.inherits("NiNode") ) {
		if ( nodeBlock.hasName("NiLODNode") ) {
			node = new LODNode( this, nodeBlock );
		} else if ( nodeBlock.hasName("NiBillboardNode") ) {
			node = new BillboardNode( this, nodeBlock );
		} else {
			node = new Node( this, nodeBlock );
		}

	} else if ( nodeBlock.hasName("NiTriShape", "NiTriStrips") || nodeBlock.inherits("NiTriBasedGeom") ) {
		node = new Mesh( this, nodeBlock );

	} else if ( nodeBlock.model()->checkVersion( 0x14050000, 0 ) && nodeBlock.hasName("NiMesh") ) {
		node = new Mesh( this, nodeBlock );

	// } else if ( nodeBlock.inherits("AParticleNode", "AParticleSystem") ) {
	// ... where did AParticleSystem go?

	} else if ( nodeBlock.inherits("NiParticles") ) {
		node = new Particles( this, nodeBlock );

	} else if ( nodeBlock.inherits("BSTriShape") ) {
		node = new BSShape( this, nodeBlock );

	} else if ( nodeBlock.inherits("BSGeometry") ) {
		node = new BSMesh( this, nodeBlock );
	}

	if ( node ) {
		nodes.add( node );
		node->update();
	}

	return node;
}

Property * Scene::getProperty( const NifModel * nif, const QModelIndex & iProperty )
{
	Property * prop = properties.get( iProperty );
	if ( prop )
		return prop;

	prop = Property::create( this, nif, iProperty );
	if ( prop )
		properties.add( prop );
	return prop;
}

Property * Scene::getProperty( const NifModel * nif, const QModelIndex & iParentBlock, const QString & itemName, const QString & mustInherit )
{
	QModelIndex iPropertyBlock = nif->getBlockIndex( nif->getLink(iParentBlock, itemName) );
	if ( iPropertyBlock.isValid() && nif->blockInherits(iPropertyBlock, mustInherit) )
		return getProperty( nif, iPropertyBlock );
	return nullptr;
}

void Scene::setSequence( const QString & seqname )
{
	animGroup = seqname;

	for ( Node * node : nodes.list() ) {
		node->setSequence( seqname );
	}
	for ( Property * prop : properties.hash() ) {
		prop->setSequence( seqname );
	}

	timeBoundsValid = false;
}

void Scene::transform( const Transform & trans, float time )
{
	view = trans;
	this->time = time;

	worldTrans.clear();
	viewTrans.clear();
	bhkBodyTrans.clear();

	for ( Property * prop : properties.hash() ) {
		prop->transform();
	}
	for ( Node * node : roots.list() ) {
		node->transform();
	}
	for ( Node * node : roots.list() ) {
		node->transformShapes();
	}

	sceneBoundsValid = false;

	// TODO: purge unused textures
}

void Scene::draw()
{
	drawShapes();

	if ( hasOption(ShowNodes) )
		drawNodes();
	if ( hasOption(ShowCollision) )
		drawHavok();
	if ( hasOption(ShowMarkers) )
		drawFurn();

	drawSelection();
}

void Scene::drawShapes()
{
	if ( hasOption(DoBlending) ) {
		NodeList secondPass;

		for ( Node * node : roots.list() ) {
			node->drawShapes( &secondPass );
		}

		if ( secondPass.list().count() > 0 )
			drawSelection(); // for transparency pass

		secondPass.alphaSort();

		for ( Node * node : secondPass.list() ) {
			node->drawShapes();
		}
	} else {
		for ( Node * node : roots.list() ) {
			node->drawShapes();
		}
	}
}

void Scene::drawNodes()
{
	for ( Node * node : roots.list() ) {
		node->draw();
	}
}

void Scene::drawHavok()
{
	for ( Node * node : roots.list() ) {
		node->drawHavok();
	}
}

void Scene::drawFurn()
{
	for ( Node * node : roots.list() ) {
		node->drawFurn();
	}
}

void Scene::drawSelection() const
{
	if ( Node::SELECTING )
		return; // do not render the selection when selecting

	for ( Node * node : nodes.list() ) {
		node->drawSelection();
	}
}

int Scene::registerShape( Shape * shape )
{
	shapes.append( shape );
	return shapes.count() - 1;
}

BoundSphere Scene::bounds() const
{
	if ( !sceneBoundsValid ) {
		bndSphere = BoundSphere();
		for ( Node * node : nodes.list() ) {
			if ( node->isVisible() )
				bndSphere |= node->bounds();
		}
		sceneBoundsValid = true;
	}

	return bndSphere;
}

void Scene::updateTimeBounds() const
{
	if ( !nodes.list().isEmpty() ) {
		tMin = +1000000000; tMax = -1000000000;
		for ( Node * node : nodes.list() ) {
			node->timeBounds( tMin, tMax );
		}
		for ( Property * prop : properties.hash() ) {
			prop->timeBounds( tMin, tMax );
		}
	} else {
		tMin = tMax = 0;
	}

	timeBoundsValid = true;
}

float Scene::timeMin() const
{
	if ( animTags.contains( animGroup ) ) {
		if ( animTags[ animGroup ].contains( "start" ) )
			return animTags[ animGroup ][ "start" ];
	}

	if ( !timeBoundsValid )
		updateTimeBounds();

	return ( tMin > tMax ? 0 : tMin );
}

float Scene::timeMax() const
{
	if ( animTags.contains( animGroup ) ) {
		if ( animTags[ animGroup ].contains( "end" ) )
			return animTags[ animGroup ][ "end" ];
	}

	if ( !timeBoundsValid )
		updateTimeBounds();

	return ( tMin > tMax ? 0 : tMax );
}

QString Scene::textStats()
{
	for ( Node * node : nodes.list() ) {
		if ( node->index() == currentBlock ) {
			return node->textStats();
		}
	}
	return QString();
}

int Scene::bindTexture( const QString & fname )
{
	if ( !hasOption(DoTexturing) || fname.isEmpty() )
		return 0;

	return textures->bind( fname, game );
}

int Scene::bindTexture( const QModelIndex & iSource )
{
	if ( !hasOption(DoTexturing) || !iSource.isValid() )
		return 0;

	return textures->bind( iSource, game );
}

