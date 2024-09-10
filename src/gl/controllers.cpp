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

#include "controllers.h"

#include "gl/glscene.h"


// ControllerManager class

ControllerManager::ControllerManager( Node * _parent, NifFieldConst ctrlBlock )
	: Controller( ctrlBlock ), parent( _parent )
{
	Q_ASSERT( hasParent() );
}

void ControllerManager::updateImpl( NifFieldConst changedBlock )
{
	Controller::updateImpl( changedBlock );

	if ( changedBlock == block && hasParent() ) {
		Scene * scene = parent->scene;
		for ( auto seqEntry : block.child("Controller Sequences").iter() ) {
			auto seqBlock = seqEntry.linkBlock("NiControllerSequence");
			if ( !seqBlock )
				continue;

			QString seqName = seqBlock.child("Name").value<QString>();
			if ( !scene->animGroups.contains( seqName ) ) {
				scene->animGroups.append( seqName );

				QMap<QString, float> tags = scene->animTags[seqName];
				auto keyBlock = seqBlock.child("Text Keys").linkBlock("NiTextKeyExtraData");
				for ( auto keyEntry: keyBlock.child("Text Keys").iter() )
					tags.insert( keyEntry["Value"].value<QString>(), keyEntry["Time"].value<float>() );
				scene->animTags[seqName] = tags;
			}
		}
	}
}

void ControllerManager::setSequence( const QString & seqName )
{
	if ( !hasParent() )
		return;

	MultiTargetTransformController * multiTargetTransformer = nullptr;
	for ( Controller * c : parent->controllers ) {
		if ( c->typeId() == QStringLiteral("NiMultiTargetTransformController") ) {
			multiTargetTransformer = static_cast<MultiTargetTransformController *>(c);
			break;
		}
	}

	MorphController * curMorphController = nullptr;
	int nextMorphIndex = 0;

	// TODO: All of the below does not work well with block updates
	for ( auto seqEntry : block.child("Controller Sequences").iter() ) {
		auto seqBlock = seqEntry.linkBlock("NiControllerSequence");
		if ( !seqBlock || seqBlock["Name"].value<QString>() != seqName )
			continue;

		start     = seqBlock.child("Start Time").value<float>();
		stop      = seqBlock.child("Stop Time").value<float>();
		phase     = seqBlock.child("Phase").value<float>();
		frequency = seqBlock.child("Frequency").value<float>();

		for ( auto ctrlBlockEntry : seqBlock.child("Controlled Blocks").iter() ) {			
			auto resolveStrField = [ctrlBlockEntry]( const QString & strName, const QString & offsetName ) -> QString {
				auto strField = ctrlBlockEntry.child(strName);
				if ( strField )
					return strField.value<QString>();

				auto offsetField = ctrlBlockEntry.child(offsetName);
				if ( offsetField ) {
					QModelIndex iOffset = offsetField.toIndex();
					return iOffset.sibling( iOffset.row(), NifModel::ValueCol ).data( NifSkopeDisplayRole ).toString();
				}

				return QString();
			};

			QString nodeName;
			auto targetNameField = ctrlBlockEntry.child("Target Name");
			if ( targetNameField ) {
				nodeName = targetNameField.value<QString>();
			} else {
				nodeName = resolveStrField( QStringLiteral("Node Name"), QStringLiteral("Node Name Offset") );
			}
			if ( nodeName.isEmpty() )
				continue;
			Node * node = parent->findChild( nodeName );
			if ( !node )
				continue;

			auto interpBlock = ctrlBlockEntry.child("Interpolator").linkBlock("NiInterpolator");
			auto controllerBlock = ctrlBlockEntry.child("Controller").linkBlock("NiTimeController");

			QString ctrlType = resolveStrField( QStringLiteral("Controller Type"), QStringLiteral("Controller Type Offset") );
			if ( ctrlType.isEmpty() && controllerBlock )
				ctrlType = controllerBlock.name();

			if ( multiTargetTransformer && ctrlType == QStringLiteral("NiTransformController") ) {
				if ( multiTargetTransformer->setNodeInterpolator( node, interpBlock ) ) {
					multiTargetTransformer->start = start;
					multiTargetTransformer->stop = stop;
					multiTargetTransformer->phase = phase;
					multiTargetTransformer->frequency = frequency;
					continue;
				}
			}

			if ( ctrlType == QStringLiteral("NiGeomMorpherController") ) {
				auto ctrl = node->findController( controllerBlock );
				if ( ctrl && ctrl->typeId() == ctrlType ) {
					if ( ctrl != curMorphController ) {
						curMorphController = static_cast<MorphController *>( ctrl );
						nextMorphIndex = 0;
					}
					curMorphController->setMorphInterpolator( nextMorphIndex, interpBlock );
					nextMorphIndex++;
				}
				continue;
			}

			QString propType = resolveStrField( QStringLiteral("Property Type"), QStringLiteral("Property Type Offset") );

			if ( ctrlType == QStringLiteral("BSLightingShaderPropertyFloatController")
				|| ctrlType == QStringLiteral("BSLightingShaderPropertyColorController")
				|| ctrlType == QStringLiteral("BSEffectShaderPropertyFloatController")
				|| ctrlType == QStringLiteral("BSEffectShaderPropertyColorController")
				|| ctrlType == QStringLiteral("BSNiAlphaPropertyTestRefController") )
			{
				auto ctrl = node->findPropertyController( propType, controllerBlock );
				if ( ctrl )
					ctrl->setInterpolator( interpBlock );
				continue;
			}

			QString var1 = resolveStrField( QStringLiteral("Controller ID"), QStringLiteral("Controller ID Offset") );
			QString var2 = resolveStrField( QStringLiteral("Interpolator ID"), QStringLiteral("Interpolator ID Offset") );
			Controller * ctrl = node->findPropertyController( propType, ctrlType, var1, var2 );
			if ( ctrl ) {
				ctrl->start = start;
				ctrl->stop = stop;
				ctrl->phase = phase;
				ctrl->frequency = frequency;

				ctrl->setInterpolator( interpBlock );
			}
		}
	}
}


// TransformInterpolator class

void TransformInterpolator::updateDataImpl()
{
	auto dataBlock = interpolatorBlock.child("Data").linkBlock("NiKeyframeData");
	registerUpdateBlock( dataBlock );

	rotation.updateData( dataBlock );
	translation.updateData( dataBlock["Translations"] );
	scale.updateData( dataBlock["Scales"] );
}

void TransformInterpolator::applyTransformImpl( float time )
{
	Transform & transform = target()->local;

	rotation.interpolate( transform.rotation, time );
	translation.interpolate( transform.translation, time );
	scale.interpolate( transform.scale, time );
}


// BSplineInterpolator class

/*********************************************************************
Simple b-spline curve algorithm

Copyright 1994 by Keith Vertanen (vertankd@cda.mrs.umn.edu)

Released to the public domain (your mileage may vary)

Found at: Programmers Heaven (www.programmersheaven.com/zone3/cat415/6660.htm)
Modified by: Theo
- reformat and convert doubles to floats
- removed point structure in favor of arbitrary sized float array
**********************************************************************/

// Used to enable static arrays to be members of vectors
struct SplineArraySlice
{
	SplineArraySlice( NifFieldConst _arrayRoot, uint _off = 0 )
		: arrayRoot( _arrayRoot ), off( _off )
	{
	}
	SplineArraySlice( const SplineArraySlice & other, uint _off = 0 )
		: arrayRoot( other.arrayRoot ), off( other.off + _off )
	{
	}

	short operator[]( uint index ) const
	{
		return arrayRoot[index + off].value<short>();
	}

	NifFieldConst arrayRoot;
	uint off;
};

template <typename T>
struct SplineTraits
{
	// Zero data
	static T & Init( T & v )
	{
		v = T();
		return v;
	}

	// Number of control points used
	static int CountOf()
	{
		return ( sizeof(T) / sizeof(float) );
	}

	// Compute point from short array and mult/bias
	static T & Compute( T & v, SplineArraySlice & c, float mult )
	{
		float * vf = (float *)&v; // assume default data is a vector of floats. specialize if necessary.

		for ( int i = 0; i < CountOf(); ++i )
			vf[i] = vf[i] + ( float(c[i]) / float(SHRT_MAX) ) * mult;

		return v;
	}
	static T & Adjust( T & v, float mult, float bias )
	{
		float * vf = (float *)&v;  // assume default data is a vector of floats. specialize if necessary.

		for ( int i = 0; i < CountOf(); ++i )
			vf[i] = vf[i] * mult + bias;

		return v;
	}
};

template <> struct SplineTraits<Quat>
{
	static Quat & Init( Quat & v )
	{
		v = Quat(); v[0] = 0.0f; return v;
	}
	static int CountOf() { return 4; }
	static Quat & Compute( Quat & v, SplineArraySlice & c, float mult )
	{
		for ( int i = 0; i < CountOf(); ++i )
			v[i] = v[i] + ( float(c[i]) / float(SHRT_MAX) ) * mult;

		return v;
	}
	static Quat & Adjust( Quat & v, float mult, float bias )
	{
		for ( int i = 0; i < CountOf(); ++i )
			v[i] = v[i] * mult + bias;

		return v;
	}
};

// calculate the blending value
static float blend( int k, int t, int * u, float v )
{
	float value;

	if ( t == 1 ) {
		// base case for the recursion
		value = ( ( u[k] <= v ) && ( v < u[k + 1] ) ) ? 1.0f : 0.0f;
	} else {
		if ( ( u[k + t - 1] == u[k] ) && ( u[k + t] == u[k + 1] ) ) // check for divide by zero
			value = 0;
		else if ( u[k + t - 1] == u[k] )                            // if a term's denominator is zero,use just the other
			value = ( u[k + t] - v) / ( u[k + t] - u[k + 1] ) * blend( k + 1, t - 1, u, v );
		else if ( u[k + t] == u[k + 1] )
			value = (v - u[k]) / (u[k + t - 1] - u[k]) * blend( k, t - 1, u, v );
		else
			value = ( v - u[k] ) / ( u[k + t - 1] - u[k] ) * blend( k, t - 1, u, v )
			+ ( u[k + t] - v ) / ( u[k + t] - u[k + 1] ) * blend( k + 1, t - 1, u, v );
	}

	return value;
}

// figure out the knots
static void compute_intervals( int * u, int n, int t )
{
	for ( int j = 0; j <= n + t; j++ ) {
		if ( j < t )
			u[j] = 0;
		else if ( ( t <= j ) && ( j <= n ) )
			u[j] = j - t + 1;
		else if ( j > n )
			u[j] = n - t + 2;  // if n-t=-2 then we're screwed, everything goes to 0
	}
}

template <typename T>
static void compute_point( int * u, int n, int t, float v, SplineArraySlice & control, T & output, float mult, float bias )
{
	// initialize the variables that will hold our output
	int l = SplineTraits<T>::CountOf();
	SplineTraits<T>::Init( output );

	for ( int k = 0; k <= n; k++ ) {
		SplineArraySlice qa( control, k * l );
		SplineTraits<T>::Compute( output, qa, blend( k, t, u, v ) );
	}

	SplineTraits<T>::Adjust( output, mult, bias );
}

void BSplineInterpolator::updateDataImpl()
{
	startTime = interpolatorBlock["Start Time"].value<float>();
	stopTime  = interpolatorBlock["Stop Time"].value<float>();

	rotateVars.off  = interpolatorBlock["Rotation Handle"].value<uint>();
	rotateVars.mult = interpolatorBlock["Rotation Half Range"].value<float>();
	rotateVars.bias = interpolatorBlock["Rotation Offset"].value<float>();

	translationVars.off  = interpolatorBlock["Translation Handle"].value<uint>();
	translationVars.mult = interpolatorBlock["Translation Half Range"].value<float>();
	translationVars.bias = interpolatorBlock["Translation Offset"].value<float>();

	scaleVars.off  = interpolatorBlock["Scale Handle"].value<uint>();
	scaleVars.mult = interpolatorBlock["Scale Half Range"].value<float>();
	scaleVars.bias = interpolatorBlock["Scale Offset"].value<float>();

	auto splineBlock = interpolatorBlock["Spline Data"].linkBlock("NiBSplineData");
	registerUpdateBlock( splineBlock );
	controlPointsRoot = splineBlock.child("Compact Control Points");

	auto basisBlock = interpolatorBlock["Basis Data"].linkBlock("NiBSplineBasisData");
	registerUpdateBlock( basisBlock );
	nControlPoints = basisBlock["Num Control Points"].value<uint>();
}

void BSplineInterpolator::applyTransformImpl( float time )
{
	Transform & transform = target()->local;

	float interval = ( ( time - startTime ) / ( stopTime - startTime ) ) * float(nControlPoints - degree);

	Quat q = transform.rotation.toQuat();
	if ( interpolateValue<Quat>( q, interval, rotateVars ) )
		transform.rotation.fromQuat( q );

	interpolateValue<Vector3>( transform.translation, interval, translationVars );
	interpolateValue<float>( transform.scale, interval, scaleVars );
}

template <typename T>
bool BSplineInterpolator::interpolateValue( T & value, float interval, const SplineVars & vars ) const
{
	if ( !vars.isActive() )
		return false;

	SplineArraySlice subArray( controlPointsRoot, vars.off );
	int t = degree + 1;
	int n = nControlPoints - 1;
	int l = SplineTraits<T>::CountOf();

	if ( interval >= float(nControlPoints - degree) ) {
		SplineTraits<T>::Init( value );
		SplineArraySlice sa( subArray, n * l );
		SplineTraits<T>::Compute( value, sa, 1.0f );
		SplineTraits<T>::Adjust( value, vars.mult, vars.bias );
	} else {
		int * u = new int[ n + t + 1 ];
		compute_intervals( u, n, t );
		compute_point( u, n, t, interval, subArray, value, vars.mult, vars.bias );
		delete [] u;
	}

	return true;
}


// ITransformInterpolator class

static inline ITransformInterpolator * createTransformInterpolator( NifFieldConst interpolatorBlock, Node * target, Controller * parentController )
{
	if ( interpolatorBlock.hasName("NiBSplineCompTransformInterpolator") )
		return new BSplineInterpolator( interpolatorBlock, target, parentController );

	if ( interpolatorBlock.hasName("NiTransformInterpolator", "NiKeyframeController") )
		return new TransformInterpolator( interpolatorBlock, target, parentController );

	// TODO: Report invalid type
	return nullptr;
}

ITransformInterpolator * TransformController::createInterpolator( NifFieldConst interpolatorBlock )
{
	return createTransformInterpolator( interpolatorBlock, target, this );
}


// MultiTargetTransformController class

MultiTargetTransformController::MultiTargetTransformController( Node * node, NifFieldConst ctrlBlock )
	: Controller( ctrlBlock ), parent( node )
{
	Q_ASSERT( !parent.isNull() );
}

void MultiTargetTransformController::updateTime( float time )
{
	if ( active && transforms.count() > 0 ) {
		time = ctrlTime( time );
		for ( auto t : transforms )
			t->applyTransform( time );
	}
}

void MultiTargetTransformController::updateImpl( NifFieldConst changedBlock )
{
	Controller::updateImpl( changedBlock );

	if ( changedBlock == block && hasParent() ) {
		targetNodes.clear();
		Scene * scene = parent->scene;
		for ( auto extraEntry : block.child("Extra Targets").iter() )
			targetNodes.add( scene->getNode( extraEntry.linkBlock() ) );

		// Remove transforms of obsolete nodes
		for ( int i = transforms.count() - 1; i >= 0; i-- ) {
			if ( !targetNodes.has( transforms[i]->target() ) )
				removeTransformAt( i );
		}
	}

	for ( auto t : transforms )
		t->updateData( changedBlock );
}

bool MultiTargetTransformController::setNodeInterpolator( Node * node, NifFieldConst interpolatorBlock )
{
	if ( interpolatorBlock && targetNodes.has( node ) ) {
		for ( int i = transforms.count() - 1; i >= 0; i-- ) {
			if ( transforms[i]->target() == node )
				removeTransformAt( i );
		}

		auto t = createTransformInterpolator( interpolatorBlock, node, nullptr );
		t->updateData( interpolatorBlock );
		transforms.append( t );

		return true;
	}

	return false;
}

void MultiTargetTransformController::clearTransforms()
{
	qDeleteAll( transforms );
	transforms.clear();
}

void MultiTargetTransformController::removeTransformAt( int i )
{
	delete transforms[i];
	transforms.removeAt( i );
}


// VisibilityInterpolator and VisibilityController classes

void VisibilityInterpolator::updateDataImpl()
{
	auto dataBlock = getDataBlock();
	registerUpdateBlock( dataBlock );
	interpolator.updateData( dataBlock["Data"] );
}

void VisibilityInterpolator::applyTransformImpl( float time )
{
	bool isVisible;
	if ( interpolator.interpolate( isVisible, time ) )
		target()->flags.node.hidden = !isVisible;
}

VisibilityInterpolator * VisibilityController::createInterpolator( NifFieldConst interpolatorBlock )
{
	return new VisibilityInterpolator( interpolatorBlock, target, this );
}


// MorphInterpolator and MorphController classes

MorphInterpolator::MorphInterpolator( int _verticesIndex, NifFieldConst _interpolatorBlock, Shape * shape, MorphController * _parentController, NifFieldConst _morphDataEntry )
	: IControllerInterpolatorTyped<Shape>( _interpolatorBlock, shape, _parentController ),
	verticesIndex( _verticesIndex ),
	morphDataEntry( _morphDataEntry )
{
}

void MorphInterpolator::updateDataImpl()
{
	if ( interpolatorBlock.inherits("NiMorphData") ) {
		interpolator.updateData( morphDataEntry );
	} else {
		auto dataBlock = interpolatorBlock.child("Data").linkBlock("NiFloatData");
		registerUpdateBlock( dataBlock );
		interpolator.updateData( dataBlock["Data"] );
	}
}

void MorphInterpolator::applyTransformImpl( float time )
{
	float x;
	if ( interpolator.interpolate( x, time ) ) {
		if ( x > 0.0f ) {
			if ( x > 1.0f )
				x = 1.0f;

			const auto & inVerts = static_cast<MorphController *>( controller )->morphVertices[verticesIndex];
			auto & outVerts = target()->verts;
			for ( int i = 0, nVerts = std::min( inVerts.count(), outVerts.count() ); i < nVerts; i++ )
				outVerts[i] += inVerts[i] * x;
		}
	}
}

MorphController::MorphController( Shape * shape, NifFieldConst ctrlBlock )
	: Controller( ctrlBlock ), target( shape )
{
	Q_ASSERT( hasTarget() );
}

bool MorphController::isActive() const
{
	if ( active && hasTarget() ) {
		for ( auto m : morphInterpolators ) {
			if ( m && m->isActive() )
				return true;
		}
	}

	return false;
}

void MorphController::updateTime( float time )
{
	if ( isActive() ) {
		const auto & firstVerts = morphVertices[0];
		if ( target->verts.count() != firstVerts.count() )
			return; // TODO: report error

		time = ctrlTime( time );
		target->verts = firstVerts;
		for ( auto m : morphInterpolators ) {
			if ( m )
				m->applyTransform( time );
		}
		target->needUpdateBounds = true;
	}
}

void MorphController::setMorphInterpolator( int morphIndex, NifFieldConst interpolatorBlock )
{
	morphIndex -= 1;
	if ( morphIndex >= 0 && morphIndex < morphInterpolators.count() ) {
		MorphInterpolator * interpolator;
		
		// Delete previous interpolator
		interpolator = morphInterpolators[morphIndex];
		if ( interpolator )
			delete interpolator;

		// Init new interpolator
		if ( interpolatorBlock ) {
			interpolator = new MorphInterpolator( morphIndex + 1, interpolatorBlock, target, this, NifFieldConst() );
			interpolator->updateData( interpolatorBlock );
		} else {
			interpolator = nullptr;
		}
		morphInterpolators[morphIndex] = interpolator;
	}
}

void MorphController::updateImpl( NifFieldConst changedBlock )
{
	bool oldActive = isActive();

	Controller::updateImpl( changedBlock );

	if ( ( changedBlock == block || changedBlock == dataBlock ) && hasTarget() ) {
		morphVertices.clear();
		clearMorphInterpolators();

		NifFieldConst interpolatorsRoot;
		auto interpolatorWeightsRoot = block.child("Interpolator Weights");
		int iInterpolatorField = -1;
		if ( interpolatorWeightsRoot ) {
			if ( interpolatorWeightsRoot.childCount() > 0 )
				iInterpolatorField = interpolatorWeightsRoot[0]["Interpolator"].row();
		} else {
			interpolatorsRoot = block.child("Interpolators");
		}

		dataBlock = block["Data"].linkBlock("NiMorphData");
		auto morphDataRoot = dataBlock.child("Morphs");
		int nMorphs = morphDataRoot.childCount();
		if ( nMorphs > 1 ) {
			morphVertices.reserve( nMorphs );
			auto firstMorphVertsRoot = morphDataRoot[0].child("Vectors");
			morphVertices.append( firstMorphVertsRoot.array<Vector3>() );

			morphInterpolators.reserve( nMorphs - 1 );
			for ( int i = 1; i < nMorphs; i++ ) {
				auto morphEntry = morphDataRoot[i];
				auto morphVertsRoot = morphEntry.child( firstMorphVertsRoot.row() );
				IControllable::reportFieldCountMismatch( morphVertsRoot, firstMorphVertsRoot, dataBlock );
				morphVertices.append( morphVertsRoot.array<Vector3>() );

				NifFieldConst interpolatorBlock;
				if ( interpolatorWeightsRoot ) {
					interpolatorBlock = interpolatorWeightsRoot[i].child(iInterpolatorField).linkBlock("NiFloatInterpolator");
				} else if ( interpolatorsRoot ) {
					interpolatorBlock = interpolatorsRoot[i].linkBlock("NiFloatInterpolator");
				} else {
					interpolatorBlock = dataBlock;
				}

				if ( interpolatorBlock ) {
					morphInterpolators.append( new MorphInterpolator( i, interpolatorBlock, target, this, morphEntry ) );
				} else {
					morphInterpolators.append( nullptr );
				}
			}
		}
	}

	for ( auto m : morphInterpolators ) {
		if ( m )
			m->updateData( changedBlock );
	}

	// Force data update for the target if the controller goes from active to inactive.
	// This reverts all the changes that have been made by the controller.
	if ( oldActive && !isActive() && hasTarget() )
		target->update();
}

void MorphController::clearMorphInterpolators()
{
	for ( auto m : morphInterpolators ) {
		if ( m )
			delete m;
	}
	morphInterpolators.clear();
}


// UVInterpolator and UVController classes

void UVInterpolator::updateDataImpl()
{
	auto dataBlock = interpolatorBlock["Data"].linkBlock("NiUVData");
	registerUpdateBlock( dataBlock );

	auto groupRoot = dataBlock["UV Groups"];
	for ( int i = 0; i < UV_GROUPS_COUNT; i++ )
		interpolators[i].updateData( groupRoot[i] );
}

void UVInterpolator::applyTransformImpl( float time )
{
	// U trans, V trans, U scale, V scale
	// see NiUVData compound in nif.xml
	float val[UV_GROUPS_COUNT] = { 0.0, 0.0, 1.0, 1.0 };
	for ( int i = 0; i < UV_GROUPS_COUNT; i++ )
		interpolators[i].interpolate( val[i], time );

	for ( auto & uv : target()->coords[0] ) {
		// scaling/tiling applied before translation
		// Note that scaling is relative to center!
		// Gavrant: -val[0] for uv[0] and +val[1] for uv[1] were in the prev. version of the code
		uv[0] = ( uv[0] - 0.5f ) * val[2] + 0.5f - val[0];
		uv[1] = ( uv[1] - 0.5f ) * val[3] + 0.5f + val[1];
	}

	target()->needUpdateData = true; // TODO (Gavrant): it's probably wrong (because the target shape would reset its UV map then)
}

UVInterpolator * UVController::createInterpolator( NifFieldConst interpolatorBlock )
{
	return new UVInterpolator( interpolatorBlock, target, this );
}


// ParticleInterpolator and ParticleController classes

ParticleInterpolator::Gravity::Gravity( NifFieldConst block )
	: force( block["Force"].value<float>() ),
	type( block["Type"].value<int>() ),
	position( block["Position"].value<Vector3>() ),
	direction( block["Direction"].value<Vector3>() )
{
}

void ParticleInterpolator::updateDataImpl()
{
	auto ctrlBlock = controllerBlock();
	registerUpdateBlock( ctrlBlock );

	emitNode   = target()->scene->getNode( ctrlBlock["Emitter"].linkBlock() );
	emitStart  = ctrlBlock["Emit Start Time"].value<float>();
	emitStop   = ctrlBlock["Emit Stop Time"].value<float>();
	emitRate   = ctrlBlock.child("Birth Rate").value<float>();
	emitRadius = ctrlBlock["Emitter Dimensions"].value<Vector3>();
	emitAccu   = 0;
	emitLast   = emitStart;

	spd = ctrlBlock.child("Speed").value<float>();
	spdRnd = ctrlBlock["Speed Variation"].value<float>();

	ttl = ctrlBlock["Lifetime"].value<float>();
	ttlRnd = ctrlBlock["Lifetime Variation"].value<float>();

	inc = ctrlBlock["Declination"].value<float>();
	incRnd = ctrlBlock["Declination Variation"].value<float>();

	dec = ctrlBlock["Planar Angle"].value<float>();
	decRnd = ctrlBlock["Planar Angle Variation"].value<float>();

	size = ctrlBlock["Initial Size"].value<float>();

	// Particles
	float emitMax = ctrlBlock.child("Num Particles").value<int>();
	
	particles.clear();
	auto particlesRoot = ctrlBlock.child("Particles");
	int nParticles = std::min( particlesRoot.childCount(), ctrlBlock.child("Num Valid").value<int>() );
	if ( nParticles > 0 ) {
		auto firstParticle = particlesRoot[0];
		int iVelocityField   = firstParticle["Velocity"].row();
		int iAgeField        = firstParticle["Age"].row();
		int iLifeSpanField   = firstParticle["Life Span"].row();
		int iLastUpdateField = firstParticle["Last Update"].row();
		int iCodeField       = firstParticle["Code"].row();

		particles.reserve( nParticles );
		for ( int i = 0; i < nParticles; i++ ) {
			Particle particle;
			auto entry = particlesRoot[i];
			particle.velocity = entry.child(iVelocityField).value<Vector3>();
			particle.lifetime = entry.child(iAgeField).value<float>();
			particle.lifespan = entry.child(iLifeSpanField).value<float>();
			particle.lasttime = entry.child(iLastUpdateField).value<float>();
			particle.vertex   = entry.child(iCodeField).value<ushort>();
			// Display saved particle start on initial load
			particles.append( particle );
		}
	}

	if ( ctrlBlock.child("Use Birth Rate").value<bool>() == false ) {
		emitRate = emitMax / (ttl + ttlRnd * 0.5f);
	}

	// Modfiers
	grow = 0.0;
	fade = 0.0;
	colorInterpolator.clear();
	gravities.clear();

	auto modifierBlock = ctrlBlock["Particle Modifier"].linkBlock("NiParticleModifier");
	while ( modifierBlock ) {
		registerUpdateBlock( modifierBlock );

		if ( modifierBlock.hasName("NiParticleGrowFade") ) {
			grow = modifierBlock["Grow"].value<float>();
			fade = modifierBlock["Fade"].value<float>();

		} else if ( modifierBlock.hasName("NiParticleColorModifier") ) {
			auto colorDataBlock = modifierBlock["Color Data"].linkBlock("NiColorData");
			registerUpdateBlock( colorDataBlock );
			colorInterpolator.updateData( colorDataBlock["Data"] );

		} else if ( modifierBlock.hasName("NiGravity") ) {
			gravities.append( Gravity( modifierBlock ) );
		}

		modifierBlock = modifierBlock["Next Modifier"].linkBlock("NiParticleModifier");
	}
}

void ParticleInterpolator::applyTransformImpl( float time )
{
	auto & targetVerts = target()->verts;

	// TODO: rework the code below for the particles list to survive more than one loop of animations

	for ( int i = 0; i < particles.count(); ) {
		Particle & p = particles[i];

		float deltaTime = (time > p.lasttime ? time - p.lasttime : 0); //( stop - start ) - p.lasttime + localtime );
		p.lifetime += deltaTime;

		if ( p.lifetime < p.lifespan && p.vertex < targetVerts.count() ) {
			p.position = targetVerts[p.vertex];

			for ( int j = 0; j < 4; j++ )
				moveParticle( p, deltaTime / 4.0f );

			p.lasttime = time;
			i++;
		} else {
			particles.removeAt( i );
		}
	}

	if ( emitNode && emitNode->isVisible() && time >= emitStart && time <= emitStop ) {
		float emitDelta = (time > emitLast ? time - emitLast : 0);
		emitLast = time;

		emitAccu += emitDelta * emitRate;

		int num = int( emitAccu );
		if ( num > 0 ) {
			emitAccu -= num;

			while ( num-- > 0 && particles.count() < targetVerts.count() ) {
				Particle p;
				startParticle( p, time );
				particles.append( p );
			}
		}
	}

	auto & targetSizes = target()->sizes;
	auto & targetColors = target()->colors;
	for ( int i = 0; i < particles.count(); i++ ) {
		Particle & p = particles[i];
		p.vertex = i;
		targetVerts[i] = p.position;

		if ( i < targetSizes.count() )
			sizeParticle( p, targetSizes[i] );

		if ( i < targetColors.count() )
			colorParticle( p, targetColors[i] );
	}

	target()->active = particles.count();
	target()->size = size;
}

static inline float randomFloat( float r )
{
	return ( float( rand() ) / float( RAND_MAX ) ) * r;
}

static inline Vector3 randomVector( const Vector3 & v )
{
	return Vector3( v[0] * randomFloat( 1.0 ), v[1] * randomFloat( 1.0 ), v[2] * randomFloat( 1.0 ) );
}

void ParticleInterpolator::startParticle( Particle & p, float localTime )
{
	const auto & targetWorldTrans = target()->worldTrans();
	const auto & emitWorldTrans = emitNode->worldTrans();

	p.position = randomVector( emitRadius * 2 ) - emitRadius;
	p.position += targetWorldTrans.rotation.inverted() * ( emitWorldTrans.translation - targetWorldTrans.translation );

	float i = inc + randomFloat( incRnd );
	float d = dec + randomFloat( decRnd );

	p.velocity = Vector3( rand() & 1 ? sin( i ) : -sin( i ), 0, cos( i ) );

	Matrix m; m.fromEuler( 0, 0, rand() & 1 ? d : -d );
	p.velocity = m * p.velocity;

	p.velocity = p.velocity * (spd + randomFloat( spdRnd ));
	p.velocity = targetWorldTrans.rotation.inverted() * emitWorldTrans.rotation * p.velocity;

	p.lifetime = 0;
	p.lifespan = ttl + randomFloat( ttlRnd );
	p.lasttime = localTime;
}

void ParticleInterpolator::moveParticle( Particle & p, float deltaTime )
{
	for ( const Gravity & g : gravities ) {
		switch ( g.type ) {
		case 0:
			p.velocity += g.direction * ( g.force * deltaTime );
			break;
		case 1:
		{
			Vector3 dir = ( g.position - p.position );
			dir.normalize();
			p.velocity += dir * ( g.force * deltaTime );
		} break;
		default:
			// TODO: report unsupported value of g.type?
			break;
		}
	}
	p.position += p.velocity * deltaTime;
}

void ParticleInterpolator::sizeParticle( Particle & p, float & sz )
{
	sz = 1.0;

	if ( grow > 0 && p.lifetime < grow )
		sz *= p.lifetime / grow;

	if ( fade > 0 && p.lifespan - p.lifetime < fade )
		sz *= (p.lifespan - p.lifetime) / fade;
}

void ParticleInterpolator::colorParticle( Particle & p, Color4 & color )
{
	colorInterpolator.interpolate( color, p.lifetime / p.lifespan );
}

ParticleInterpolator * ParticleController::createInterpolator( NifFieldConst interpolatorBlock )
{
	return new ParticleInterpolator( interpolatorBlock, target, this );
}


// AlphaInterpolator_Material and AlphaController_Material classes

void AlphaInterpolator_Material::updateDataImpl()
{
	auto dataBlock = getDataBlock();
	registerUpdateBlock( dataBlock );
	interpolator.updateData( dataBlock["Data"] );
}

void AlphaInterpolator_Material::applyTransformImpl( float time )
{
	float val;
	if ( interpolator.interpolate( val, time ) )
		target()->alpha = std::clamp( val, 0.0f, 1.0f );
}

AlphaInterpolator_Material * AlphaController_Material::createInterpolator( NifFieldConst interpolatorBlock )
{
	return new AlphaInterpolator_Material( interpolatorBlock, target, this );
}


// AlphaInterpolator_Alpha and AlphaController_Alpha classes

void AlphaInterpolator_Alpha::updateDataImpl()
{
	auto dataBlock = getDataBlock();
	registerUpdateBlock( dataBlock );
	interpolator.updateData( dataBlock["Data"] );
}

void AlphaInterpolator_Alpha::applyTransformImpl( float time )
{
	float val;
	if ( interpolator.interpolate( val, time ) )
		target()->alphaThreshold = std::clamp( val / 255.0f, 0.0f, 1.0f );
}

AlphaInterpolator_Alpha * AlphaController_Alpha::createInterpolator( NifFieldConst interpolatorBlock )
{
	return new AlphaInterpolator_Alpha( interpolatorBlock, target, this );
}


// MaterialColorInterpolator and MaterialColorController classes

void MaterialColorInterpolator::updateDataImpl()
{
	auto ctrlBlock = controllerBlock();
	registerUpdateBlock( ctrlBlock );

	auto fieldColor = ctrlBlock.child("Target Color");
	if ( fieldColor ) {
		colorType = ColorType( fieldColor.value<int>() );
	} else {
		colorType = ColorType( ( ctrlBlock["Flags"].value<int>() >> 4 ) & 7 );
	}
	// TODO: validate colorType value?

	auto dataBlock = getDataBlock();
	registerUpdateBlock( dataBlock );
	interpolator.updateData( dataBlock["Data"] );
}

void MaterialColorInterpolator::applyTransformImpl( float time )
{
	Vector3 val;
	if ( interpolator.interpolate( val, time ) ) {
		Color4 color( val, 1.0 );
		switch ( colorType ) {
		case ColorType::Ambient:
			target()->ambient = color;
			break;
		case ColorType::Diffuse:
			target()->diffuse = color;
			break;
		case ColorType::Specular:
			target()->specular = color;
			break;
		case ColorType::SelfIllum:
			target()->emissive = color;
			break;
		}
	}
}

MaterialColorInterpolator * MaterialColorController::createInterpolator( NifFieldConst interpolatorBlock )
{
	return new MaterialColorInterpolator( interpolatorBlock, target, this );
}


// TextureFlipData struct

void TextureFlipData::updateData( IControllerInterpolator * ctrlInterpolator, const QString & sourcesName, const QString & sourceBlockType )
{
	auto ctrlBlock = ctrlInterpolator->controllerBlock();
	ctrlInterpolator->registerUpdateBlock( ctrlBlock );
	slot = ctrlBlock["Texture Slot"].value<int>();

	auto deltaField = ctrlBlock.child("Delta");
	hasDelta = deltaField.isValid();
	delta = deltaField.value<float>();

	sources.clear();
	auto sourcesRoot = ctrlBlock[sourcesName];
	sources.reserve( sourcesRoot.childCount() );
	for ( auto entry : sourcesRoot.iter() )
		sources.append( entry.linkBlock(sourceBlockType) );

	auto dataBlock = ctrlInterpolator->getDataBlock();
	ctrlInterpolator->registerUpdateBlock( dataBlock );
	interpolator.updateData( dataBlock["Data"] );
}

void TextureFlipData::interpolate( NifFieldConst & sourceBlock, float time )
{
	if ( hasDelta ) {
		if ( delta > 0.0f )
			sourceBlock = sources[time / delta];
	} else {
		float r = 0;
		if ( interpolator.interpolate( r, time ) )
			sourceBlock = sources[r];
	}
}


// TextureFlipInterpolator_Texturing and TextureFlipController_Texturing classes

void TextureFlipInterpolator_Texturing::updateDataImpl()
{
	data.updateData( this, QStringLiteral("Sources"), QStringLiteral("NiSourceTexture") );
}

void TextureFlipInterpolator_Texturing::applyTransformImpl( float time )
{
	data.interpolate( target()->textures[data.slot & 7].sourceBlock, time );
}

TextureFlipInterpolator_Texturing * TextureFlipController_Texturing::createInterpolator( NifFieldConst interpolatorBlock )
{
	return new TextureFlipInterpolator_Texturing( interpolatorBlock, target, this );
}


// TextureFlipInterpolator_Texture and TextureFlipController_Texture classes

void TextureFlipInterpolator_Texture::updateDataImpl()
{
	data.updateData( this, QStringLiteral("Images"), QStringLiteral("NiImage") );
}

void TextureFlipInterpolator_Texture::applyTransformImpl( float time )
{
	data.interpolate( target()->imageBlock, time );
}

TextureFlipInterpolator_Texture * TextureFlipController_Texture::createInterpolator( NifFieldConst interpolatorBlock )
{
	return new TextureFlipInterpolator_Texture( interpolatorBlock, target, this );
}


// TextureTransformInterpolator and TextureTransformController classes

void TextureTransformInterpolator::updateDataImpl()
{
	auto ctrlBlock = controllerBlock();
	registerUpdateBlock( ctrlBlock );
	operationType = OperationType( ctrlBlock["Operation"].value<int>() );
	// TODO: validate operationType value?
	textureSlot = ctrlBlock["Texture Slot"].value<int>();

	auto dataBlock = getDataBlock();
	registerUpdateBlock( dataBlock );
	interpolator.updateData( dataBlock["Data"] );
}

void TextureTransformInterpolator::applyTransformImpl( float time )
{
	float val;
	if ( interpolator.interpolate( val, time ) ) {
		TexturingProperty::TexDesc * tex = target()->textures + (textureSlot & 7);

		// If desired, we could force display even if texture transform was disabled:
		// tex->hasTransform = true;
		// however "Has Texture Transform" doesn't exist until 10.1.0.0, and neither does
		// NiTextureTransformController - so we won't bother
		switch ( operationType ) {
		case OperationType::TranslateU:
			tex->translation[0] = val;
			break;
		case OperationType::TranslateV:
			tex->translation[1] = val;
			break;
		case OperationType::Rotate:
			tex->rotation = val;
			break;
		case OperationType::ScaleU:
			tex->tiling[0] = val;
			break;
		case OperationType::ScaleV:
			tex->tiling[1] = val;
			break;
		}
	}
}

TextureTransformInterpolator * TextureTransformController::createInterpolator( NifFieldConst interpolatorBlock )
{
	return new TextureTransformInterpolator( interpolatorBlock, target, this );
}


// EffectFloatInterpolator and EffectFloatController classes

void EffectFloatInterpolator::updateDataImpl()
{
	auto ctrlBlock = controllerBlock();
	registerUpdateBlock( ctrlBlock );
	valueType = ValueType( ctrlBlock["Controlled Variable"].value<int>() );
	// TODO: validate valueType value?

	auto dataBlock = getDataBlock();
	registerUpdateBlock( dataBlock );
	interpolator.updateData( dataBlock["Data"] );
}

void EffectFloatInterpolator::applyTransformImpl( float time )
{
	float val;
	if ( interpolator.interpolate( val, time ) ) {
		switch ( valueType ) {
		case ValueType::Emissive_Multiple:
			target()->emissiveMult = val;
			break;
		case ValueType::Falloff_Start_Angle:
			target()->falloff.startAngle = val;
			break;
		case ValueType::Falloff_Stop_Angle:
			target()->falloff.stopAngle = val;
			break;
		case ValueType::Falloff_Start_Opacity:
			target()->falloff.startOpacity = val;
			break;
		case ValueType::Falloff_Stop_Opacity:
			target()->falloff.stopOpacity = val;
			break;
		case ValueType::Alpha:
			target()->emissiveColor.setAlpha( val );
			break;
		case ValueType::U_Offset:
			target()->uvOffset.x = val;
			break;
		case ValueType::U_Scale:
			target()->uvScale.x = val;
			break;
		case ValueType::V_Offset:
			target()->uvOffset.y = val;
			break;
		case ValueType::V_Scale:
			target()->uvScale.y = val;
			break;
		}
	}
}

EffectFloatInterpolator * EffectFloatController::createInterpolator( NifFieldConst interpolatorBlock )
{
	return new EffectFloatInterpolator( interpolatorBlock, target, this );
}


// EffectColorInterpolator and EffectColorController classes

void EffectColorInterpolator::updateDataImpl()
{
	auto ctrlBlock = controllerBlock();
	registerUpdateBlock( ctrlBlock );
	colorType = ctrlBlock["Controlled Color"].value<int>();
	// TODO: validate variable value?

	auto dataBlock = getDataBlock();
	registerUpdateBlock( dataBlock );
	interpolator.updateData( dataBlock["Data"] );
}

void EffectColorInterpolator::applyTransformImpl( float time )
{
	Vector3 val;
	if ( interpolator.interpolate( val, time ) ) {
		switch ( colorType ) {
		case 0:
			target()->emissiveColor = Color4( val, target()->emissiveColor.alpha() );
			break;
		}
	}
}

EffectColorInterpolator * EffectColorController::createInterpolator( NifFieldConst interpolatorBlock )
{
	return new EffectColorInterpolator( interpolatorBlock, target, this );
}


// LightingFloatInterpolator and LightingFloat LightingFloatController classes

void LightingFloatInterpolator::updateDataImpl()
{
	auto ctrlBlock = controllerBlock();
	registerUpdateBlock( ctrlBlock );
	valueType = ValueType( ctrlBlock["Controlled Variable"].value<int>() );
	// TODO: validate valueType value?

	auto dataBlock = getDataBlock();
	registerUpdateBlock( dataBlock );
	interpolator.updateData( dataBlock["Data"] );
}

void LightingFloatInterpolator::applyTransformImpl( float time )
{
	float val;
	if ( interpolator.interpolate( val, time ) ) {
		switch ( valueType ) {
		case ValueType::Refraction_Strength:
			break;
		case ValueType::Reflection_Strength:
			target()->environmentReflection = val;
			break;
		case ValueType::Glossiness:
			target()->specularGloss = val;
			break;
		case ValueType::Specular_Strength:
			target()->specularStrength = val;
			break;
		case ValueType::Emissive_Multiple:
			target()->emissiveMult = val;
			break;
		case ValueType::Alpha:
			target()->alpha = val;
			break;
		case ValueType::U_Offset:
			target()->uvOffset.x = val;
			break;
		case ValueType::U_Scale:
			target()->uvScale.x = val;
			break;
		case ValueType::V_Offset:
			target()->uvOffset.y = val;
			break;
		case ValueType::V_Scale:
			target()->uvScale.y = val;
			break;
		}
	}
}

LightingFloatInterpolator * LightingFloatController::createInterpolator( NifFieldConst interpolatorBlock )
{
	return new LightingFloatInterpolator( interpolatorBlock, target, this );
}


// LightingColorInterpolator and LightingColorController classes

void LightingColorInterpolator::updateDataImpl()
{
	auto ctrlBlock = controllerBlock();
	registerUpdateBlock( ctrlBlock );
	colorType = ctrlBlock["Controlled Color"].value<int>();
	// TODO: validate colorType value?

	auto dataBlock = getDataBlock();
	registerUpdateBlock( dataBlock );
	interpolator.updateData( dataBlock["Data"] );
}

void LightingColorInterpolator::applyTransformImpl( float time )
{
	Vector3 val;
	if ( interpolator.interpolate( val, time ) ) {
		switch ( colorType ) {
		case 0:
			target()->specularColor.fromVector3( val );
			break;
		case 1:
			target()->emissiveColor.fromVector3( val );
			break;
		default:
			break;
		}
	}
}

LightingColorInterpolator * LightingColorController::createInterpolator( NifFieldConst interpolatorBlock )
{
	return new LightingColorInterpolator( interpolatorBlock, target, this );
}
