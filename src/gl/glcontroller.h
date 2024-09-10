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

#ifndef GLCONTROLLER_H
#define GLCONTROLLER_H

#include "model/nifmodel.h"

#include <QPersistentModelIndex>
#include <QPointer>


class IControllable;

//! A block which can be attached to anything Controllable
class Controller
{
public:
	const NifFieldConst block;

	float start = 0;
	float stop = 0;
	float phase = 0;
	float frequency = 0;

	enum class ExtrapolationType
	{
		Cyclic = 0,
		Reverse = 1,
		Constant = 2
	};
	ExtrapolationType extrapolation = ExtrapolationType::Cyclic;

	bool active = false;

protected:
	QPersistentModelIndex iBlock;

public:
	Controller( NifFieldConst ctrlBlock );
	virtual ~Controller() {}

	// Get the block type of the controller
	const QString & typeId() const { return block.name(); }

	// Get the model index of the controller
	QModelIndex index() const { return iBlock; } // TODO: Get rid of it

	bool isValid() const { return iBlock.isValid(); }

	// Set the interpolator from its block
	virtual void setInterpolator( NifFieldConst newInterpolatorBlock );

	// Set sequence name for animation groups
	virtual void setSequence( const QString & seqName );

	// Update for model and index
	void update( NifFieldConst changedBlock );
	void update() { update( block ); }

	//! Update for specified time
	virtual void updateTime( float time ) = 0;

protected:
	virtual void updateImpl( NifFieldConst changedBlock );

	// Determine the controller time based on the specified time
	float ctrlTime( float time ) const;

	static NifFieldConst getInterpolatorBlock( NifFieldConst controllerBlock );
};


// Value interpolator template
template <typename T>
class ValueInterpolator final
{
	enum class InterpolationMode
	{
		Unknown = -1,
		Linear = 1,
		Quadratic = 2,
		TBC = 3, // Tension Bias Continuity
		XyzRotation = 4,
		Const = 5,
	};
	InterpolationMode interpolationMode = InterpolationMode::Unknown;

	struct Key
	{
		float time;
		T value;
		T backward;
		T forward;

		Key( NifFieldConst keyRoot, int iTimeField, int iValueField, int iBackwardField, int iForwardField );
	};
	using ConstKeyPtr = const Key *;

	QVector<Key> keys;
	int keyIndexCache = 0;

public:
	void clear();

	bool isActive() const { return keys.count() > 0; }

	void updateData( NifFieldConst keyGroup );
	bool interpolate( T & value, float time );

private:
	bool getFrame( float inTime, ConstKeyPtr & prevKey, ConstKeyPtr & nextKey, float & fraction );
};

using ValueInterpolatorBool    = ValueInterpolator<bool>;
using ValueInterpolatorFloat   = ValueInterpolator<float>;
using ValueInterpolatorVector3 = ValueInterpolator<Vector3>;
using ValueInterpolatorColor3  = ValueInterpolator<Color3>;
using ValueInterpolatorColor4  = ValueInterpolator<Color4>;


// Matrix value interpolator
class ValueInterpolatorMatrix final
{
	static constexpr int EULER_COUNT = 3;

	QVector<ValueInterpolatorFloat> eulers;
	ValueInterpolator<Quat> quat;

public:
	void clear();

	bool isActive() const;

	void updateData( NifFieldConst keyGroup );
	bool interpolate( Matrix & value, float time );
};


// Base interface for controller interpolators
class IControllerInterpolator
{
public:
	const NifFieldConst interpolatorBlock;
	Controller * const controller;

protected:
	QPointer<IControllable> targetControllable;

private:
	bool needDataUpdate = true;
	QVector<NifFieldConst> updateBlocks; // (Extra) blocks that trigger updateDataImpl()

public:
	IControllerInterpolator( NifFieldConst _interpolatorBlock, IControllable * _targetControllable, Controller * _parentController );
	IControllerInterpolator() = delete;
	IControllerInterpolator( const IControllerInterpolator & ) = delete;

	bool hasTarget() const { return !targetControllable.isNull(); }

	NifFieldConst controllerBlock() const { return controller ? controller->block : NifFieldConst(); }

	NifFieldConst getDataBlock() const { return interpolatorBlock.child("Data").linkBlock(); }

	void registerUpdateBlock( NifFieldConst updateBlock )
	{
		if ( updateBlock )
			updateBlocks.append( updateBlock );
	}

	void updateData( NifFieldConst changedBlock );

	void applyTransform( float time )
	{
		if ( hasTarget() )
			applyTransformImpl( time );
	}

protected:
	virtual void updateDataImpl() = 0;
	virtual void applyTransformImpl( float time ) = 0;
};


// Template for a controller interpolator targetting IControllables of certain type
template<typename ControllableType>
class IControllerInterpolatorTyped : public IControllerInterpolator
{
public:
	IControllerInterpolatorTyped( NifFieldConst _interpolatorBlock, ControllableType * _targetControllable, Controller * _parentController )
		: IControllerInterpolator( _interpolatorBlock, _targetControllable, _parentController ) {}

	ControllableType * target() const { return static_cast<ControllableType *>( targetControllable.data() ); }

	virtual bool isActive() const = 0;
};


// Template for a simple Controller with a target IControllable and a controller interpolator
template<typename ControllableType, typename InterpolatorType>
class InterpolatedController : public Controller
{
protected:
	InterpolatorType * interpolator = nullptr;
	QPointer<ControllableType> target;

public:
	InterpolatedController( ControllableType * _targetControllable, NifFieldConst ctrlBlock )
		: Controller( ctrlBlock ), target( _targetControllable )
	{
		Q_ASSERT( hasTarget() ); 
	}
	virtual ~InterpolatedController() { clearInterpolator(); }

	bool hasValidInterpolator() const { return interpolator && interpolator->hasTarget(); }

	bool hasTarget() const { return !target.isNull(); }

	bool isActive() const { return active && hasTarget() && hasValidInterpolator() && interpolator->isActive(); }

	void setInterpolator( NifFieldConst newInterpolatorBlock ) override final
	{
		setInterpolatorImpl( newInterpolatorBlock, true );
	}

	void updateTime( float time ) override final
	{
		if ( isActive() )
			interpolator->applyTransform( ctrlTime( time ) );
	}

protected:
	void updateImpl( NifFieldConst changedBlock ) override final
	{
		bool oldActive = isActive();

		Controller::updateImpl( changedBlock );

		if ( changedBlock == block && hasTarget() )
			setInterpolatorImpl( getInterpolatorBlock( block ), false );

		if ( hasValidInterpolator() )
			interpolator->updateData( changedBlock );

		// Force data update for the target if the controller goes from active to inactive.
		// This reverts all the changes that have been made by the controller.
		if ( oldActive && !isActive() && hasTarget() )
			target->update();
	}

	void clearInterpolator()
	{
		if ( interpolator ) {
			delete interpolator;
			interpolator = nullptr;
		}
	}

	void setInterpolatorImpl( NifFieldConst newInterpolatorBlock, bool instantDataUpdate )
	{
		if ( !hasTarget() || !newInterpolatorBlock ) {
			clearInterpolator();
		} else if ( interpolator && interpolator->interpolatorBlock == newInterpolatorBlock ) {
			// No change, do nothing
		} else {
			clearInterpolator();
			interpolator = createInterpolator( newInterpolatorBlock );
			if ( interpolator && instantDataUpdate )
				interpolator->updateData( newInterpolatorBlock );
		}
	}

	virtual InterpolatorType * createInterpolator( NifFieldConst interpolatorBlock ) = 0;
};

#define DECLARE_INTERPOLATED_CONTROLLER( ControllerType, ControllableType, InterpolatorType ) \
	class ControllerType final : public InterpolatedController<ControllableType, InterpolatorType> \
	{ \
	public: \
		ControllerType( ControllableType * _targetControllable, NifFieldConst ctrlBlock ) \
			: InterpolatedController( _targetControllable, ctrlBlock ) {} \
	protected: \
		InterpolatorType * createInterpolator( NifFieldConst interpolatorBlock ) override final; \
	};

#endif


