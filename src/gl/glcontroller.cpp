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

#include "glcontroller.h"

#include "gl/glscene.h"


//! @file glcontroller.cpp Controllable management, Interpolation management

Controller::Controller( NifFieldConst ctrlBlock )
	: block( ctrlBlock ), iBlock( ctrlBlock.toIndex() )
{
	Q_ASSERT( block.isBlock() );
}

void Controller::setSequence( [[maybe_unused]] const QString & seqName )
{
}

void Controller::setInterpolator( [[maybe_unused]] NifFieldConst newInterpolatorBlock )
{
}

void Controller::update( NifFieldConst changedBlock )
{
	if ( isValid() ) {
		updateImpl( changedBlock );
	}
}

void Controller::updateImpl( NifFieldConst changedBlock )
{
	if ( changedBlock == block ) {
		start     = block.child("Start Time").value<float>();
		stop      = block.child("Stop Time").value<float>();
		phase     = block.child("Phase").value<float>();
		frequency = block.child("Frequency").value<float>();

		int flags = block.child("Flags").value<int>();
		active = flags & 0x08;
		extrapolation = ExtrapolationType( ( flags & 0x06 ) >> 1 );

		// TODO: Bit 4 (16) - Plays entire animation backwards.
		// TODO: Bit 5 (32) - Generally only set when sequences are present.
		// TODO: Bit 6 (64) - Always seems to be set on Skyrim NIFs, unknown function.
	}
}

float Controller::ctrlTime( float time ) const
{
	time = frequency * time + phase;
	if ( time >= start && time <= stop )
		return time;

	switch ( extrapolation ) {
	case ExtrapolationType::Cyclic:
		{
			float delta = stop - start;
			if ( delta <= 0 )
				return start;

			float x = ( time - start ) / delta;
			float y = ( x - floor( x ) ) * delta;

			return start + y;
		}

	case ExtrapolationType::Reverse:
		{
			float delta = stop - start;
			if ( delta <= 0 )
				return start;

			float x = ( time - start ) / delta;
			float y = ( x - floor( x ) ) * delta;

			if ( ( ( (int)fabs( floor( x ) ) ) & 1 ) == 0 )
				return start + y;

			return stop - y;
		}

	case ExtrapolationType::Constant:
	default:

		if ( time < start )
			return start;

		if ( time > stop )
			return stop;

		return time;
	}
}

NifFieldConst Controller::getInterpolatorBlock( NifFieldConst controllerBlock )
{
	auto interpolatorField = controllerBlock.child("Interpolator");
	if ( interpolatorField )
		return interpolatorField.linkBlock();

	if ( controllerBlock.child("Data") ) // Support for old controllers
		return controllerBlock; 

	return NifFieldConst();
}


// ValueInterpolator class

template <typename T> 
ValueInterpolator<T>::Key::Key( NifFieldConst keyRoot, int iTimeField, int iValueField, int iBackwardField, int iForwardField )
	: time( keyRoot[iTimeField].value<float>() ),
	value( keyRoot[iValueField].value<T>() ),
	backward( keyRoot.child(iBackwardField).value<T>() ),
	forward( keyRoot.child(iForwardField).value<T>() )
{
}

template <typename T> 
void ValueInterpolator<T>::clear()
{
	keys.clear();
}

template <typename T> 
void ValueInterpolator<T>::updateData( NifFieldConst keyGroup  )
{
	interpolationMode = InterpolationMode::Unknown;
	keys.clear();

	NifFieldConst keyArrayRoot;
	if ( keyGroup.hasStrType("KeyGroup", "Morph") ) {
		keyArrayRoot = keyGroup.child("Keys");
		auto modeField = keyGroup.child("Interpolation");
		if ( modeField )
			interpolationMode = InterpolationMode( modeField.value<int>() );

	} else if ( keyGroup.hasStrType("QuatKey") ) {
		keyArrayRoot = keyGroup;

	} else {
		if ( keyGroup )
			keyGroup.reportError( QString("Invalid or unsupported interpolator key group type '%1'.").arg( keyGroup.strType() ) );
	}

	int nKeys = keyArrayRoot.childCount();
	if ( nKeys > 0 ) {
		auto firstKey = keyArrayRoot[0];

		int iTimeField  = firstKey["Time"].row();
		int iValueField = firstKey["Value"].row();
		if ( iTimeField >= 0 && iValueField >= 0 ) {
			int iBackwardField = firstKey.child("Backward").row();
			int iForwardField  = firstKey.child("Forward").row();

			keys.reserve( nKeys );
			for ( auto keyEntry : keyArrayRoot.iter() )
				keys.append( Key( keyEntry, iTimeField, iValueField, iBackwardField, iForwardField ) );
		}
	}
}

template <typename T> 
bool ValueInterpolator<T>::getFrame( float inTime, ConstKeyPtr & pKey1, ConstKeyPtr & pKey2, float & fraction )
{
	int lastKey = keys.count() - 1;
	if ( lastKey < 0 )
		return false;

	ConstKeyPtr keyData = keys.constData();

	if ( inTime <= keyData[0].time ) {
		pKey1 = pKey2 = keyData;
	
	} else if ( inTime >= keyData[lastKey].time ) {
		pKey1 = pKey2 = keyData + lastKey;

	} else {
		int iKey = ( keyIndexCache >= 0 && keyIndexCache <= lastKey ) ? keyIndexCache : 0;
		ConstKeyPtr pKey = keyData + iKey;
		if ( pKey->time < inTime ) {
			do {
				pKey++;
			} while( pKey->time < inTime );

			pKey1 = ( pKey->time == inTime ) ? pKey : ( pKey - 1 );
			pKey2 = pKey;

		} else if ( pKey->time > inTime ) {
			do {
				pKey--;
			} while( pKey->time > inTime );

			pKey1 = pKey;
			pKey2 = ( pKey->time == inTime ) ? pKey : ( pKey + 1 );

		} else { // pKey->time == inTime
			pKey1 = pKey2 = pKey;
		}
	}

	if ( pKey1 != pKey2 ) {
		fraction = ( inTime - pKey1->time ) / ( pKey2->time - pKey1->time );
	} else {
		fraction = 0.0;
	}
	keyIndexCache = pKey1 - keyData;

	return true;
}

template <typename T> 
bool ValueInterpolator<T>::interpolate( T & value, float time )
{
	ConstKeyPtr pKey1, pKey2;
	float x;
	if ( !getFrame( time, pKey1, pKey2, x ) )
		return false;

	const T & v1 = pKey1->value;
	const T & v2 = pKey2->value;

	switch( interpolationMode )
	{
	case InterpolationMode::Quadratic:
	{
		// Quadratic
		/*
		In general, for keyframe values v1 = 0, v2 = 1 it appears that
		setting v1's corresponding "Backward" value to 1 and v2's
		corresponding "Forward" to 1 results in a linear interpolation.
		*/

		// Tangent 1
		const T & t1 = pKey1->backward;
		// Tangent 2
		const T & t2 = pKey2->forward;

		float x2 = x * x;
		float x3 = x2 * x;

		// Cubic Hermite spline
		//	x(t) = (2t^3 - 3t^2 + 1)P1  + (-2t^3 + 3t^2)P2 + (t^3 - 2t^2 + t)T1 + (t^3 - t^2)T2

		value = v1 * (2.0f * x3 - 3.0f * x2 + 1.0f) + v2 * (-2.0f * x3 + 3.0f * x2) + t1 * (x3 - 2.0f * x2 + x) + t2 * (x3 - x2);

	} break;
	
	case InterpolationMode::Const:
		// Constant
		if ( x < 0.5 )
			value = v1;
		else
			value = v2;
		break;

	default:
		value = v1 + ( v2 - v1 ) * x;
		break;
	}

	return true;
}

template <> 
bool ValueInterpolator<bool>::interpolate( bool & value, float time )
{
	ConstKeyPtr pKey1, pKey2;
	float x;
	if ( !getFrame( time, pKey1, pKey2, x ) )
		return false;

	value = pKey1->value;
	return true;
}

template <>
bool ValueInterpolator<Quat>::interpolate( Quat & value, float time )
{
	ConstKeyPtr pKey1, pKey2;
	float x;
	if ( !getFrame( time, pKey1, pKey2, x ) )
		return false;

	Quat v1 = pKey1->value;
	const Quat & v2 = pKey2->value;

	if ( Quat::dotproduct( v1, v2 ) < 0 )
		v1.negate(); // don't take the long path

	value = Quat::slerp( x, v1, v2 );
	return true;
}

// A hack to avoid "unresolved external symbol" errors for ValueInterpolator members
// https://stackoverflow.com/questions/495021/why-can-templates-only-be-implemented-in-the-header-file?noredirect=1&lq=1
template ValueInterpolator<bool>;
template ValueInterpolator<float>;
template ValueInterpolator<Vector3>;
template ValueInterpolator<Color3>;
template ValueInterpolator<Color4>;


// ValueInterpolatorMatrix class

void ValueInterpolatorMatrix::clear()
{
	eulers.clear();
	quat.clear();
}

void ValueInterpolatorMatrix::updateData( NifFieldConst keyGroup )
{
	clear();

	auto eulerRoot = keyGroup.child("XYZ Rotations");
	if ( eulerRoot ) {
		eulers.resize( EULER_COUNT );
		for ( int i = 0; i < EULER_COUNT; i++ )
			eulers[i].updateData( eulerRoot[i] );
	} else {
		quat.updateData( keyGroup["Quaternion Keys"] );
	}
}

bool ValueInterpolatorMatrix::interpolate( Matrix & value, float time )
{
	if ( eulers.count() > 0 ) {
		bool success = false;
		float r[EULER_COUNT];
		for ( int i = 0; i < EULER_COUNT; i++ ) {
			r[i] = 0.0f;
			if ( eulers[i].interpolate( r[i], time ) )
				success = true;
		}

		if ( success ) {
			value = Matrix::euler( 0, 0, r[2] ) * Matrix::euler( 0, r[1], 0 ) * Matrix::euler( r[0], 0, 0 );
			return true;
		}
	} else {
		Quat outv;
		if ( quat.interpolate( outv, time ) ) {
			value.fromQuat( outv );
			return true;
		}
	}

	return false;
}


// IControllerInterpolator class

IControllerInterpolator::IControllerInterpolator( NifFieldConst _interpolatorBlock, IControllable * _targetControllable, Controller * _parentController )
	: interpolatorBlock( _interpolatorBlock ), targetControllable( _targetControllable ), controller( _parentController )
{
	Q_ASSERT( interpolatorBlock.isBlock() );
	Q_ASSERT( hasTarget() );
}

void IControllerInterpolator::updateData( NifFieldConst changedBlock )
{
	if ( changedBlock == interpolatorBlock || needDataUpdate || updateBlocks.contains( changedBlock ) ) {
		needDataUpdate = false;
		updateBlocks.clear();
		if ( hasTarget() )
			updateDataImpl();
	}
}
