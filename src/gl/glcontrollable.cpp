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

#include "glcontrollable.h"

#include "gl/glcontroller.h"
#include "gl/glscene.h"


//! @file glcontrollable.cpp IControllable interface

IControllable::IControllable( Scene * _scene, NifFieldConst _block )
	: scene( _scene ), block( _block ), iBlock( _block.toIndex() ), model( _block.model() )
{
	Q_ASSERT( scene != nullptr );
	Q_ASSERT( block.isBlock() );
	Q_ASSERT( model != nullptr );
}

IControllable::~IControllable()
{
	qDeleteAll( controllers );
}

void IControllable::clear()
{
	name = QString();

	qDeleteAll( controllers );
	controllers.clear();
}

Controller * IControllable::createController( [[maybe_unused]] NifFieldConst controllerBlock )
{
	return nullptr;
}

Controller * IControllable::findController( const QString & ctrlType, [[maybe_unused]] const QString & var1, [[maybe_unused]] const QString & var2 ) const
{
	Controller * ctrl = nullptr;

	for ( Controller * c : controllers ) {
		if ( c->typeId() == ctrlType ) {
			if ( !ctrl ) {
				ctrl = c;
			} else {
				ctrl = nullptr;
				// TODO: eval var1 + var2 offset to determine which controller is targeted
				break;
			}
		}
	}

	return ctrl;
}

Controller * IControllable::findController( NifFieldConst ctrlBlock ) const
{
	for ( Controller * c : controllers ) {
		if ( c->block == ctrlBlock )
			return c;
	}

	return nullptr;
}

void IControllable::update( const NifModel * nif, const QModelIndex & index )
{
	if ( isValid() ) {
		updateImpl( nif, index );
	} else {
		clear();
	}
}

void IControllable::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	NifFieldConst changedBlock = nif->field(index);
	bool syncControllers = false;

	if ( changedBlock == block ) {
		name = block.child("Name").value<QString>();
		syncControllers = true;
	}

	for ( Controller * ctrl : controllers ) {
		ctrl->update( changedBlock );
		if ( changedBlock == ctrl->block )
			syncControllers = true;
	}

	// Sync the list of attached controllers
	if ( syncControllers ) {
		QList<Controller *> obsolete( controllers );

		// TODO: check if we're not stuck in an infinite controller loop
		auto ctrlField = block.child("Controller");
		while ( true ) {
			auto ctrlBlock = ctrlField.linkBlock("NiTimeController");
			if ( !ctrlBlock )
				break;

			Controller * ctrl = findController( ctrlBlock );
			if ( ctrl ) {
				obsolete.removeAll( ctrl );
			} else {
				ctrl = createController( ctrlBlock );
				if ( ctrl ) {
					controllers.append( ctrl );
					ctrl->update();
				}
			}

			ctrlField = ctrlBlock.child("Next Controller");
		}

		for ( Controller * ctrl : obsolete ) {
			controllers.removeAll( ctrl );
			delete ctrl;
		}
	}
}

void IControllable::transform()
{
	if ( scene->animate ) {
		for ( Controller * controller : controllers ) {
			controller->updateTime( scene->time );
		}
	}
}

void IControllable::timeBounds( float & tmin, float & tmax )
{
	if ( controllers.isEmpty() )
		return;

	float mn = controllers.first()->start;
	float mx = controllers.first()->stop;

	for ( Controller * c : controllers ) {
		mn = qMin( mn, c->start );
		mx = qMax( mx, c->stop );
	}
	tmin = qMin( tmin, mn );
	tmax = qMax( tmax, mx );
}

void IControllable::setSequence( const QString & seqName )
{
	for ( Controller * ctrl : controllers ) {
		ctrl->setSequence( seqName );
	}
}

void IControllable::reportFieldCountMismatch( NifFieldConst rootEntry1, int entryCount1, NifFieldConst rootEntry2, int entryCount2, NifFieldConst reportEntry )
{
	if ( rootEntry1 && rootEntry2 && entryCount1 != entryCount2 ) {
		reportEntry.reportError( 
			tr("The number of entries in %1 (%2) does not match that in %3 (%4).")
			.arg( rootEntry1.repr( reportEntry ) )
			.arg( entryCount1 )
			.arg( rootEntry2.repr( reportEntry ) )
			.arg( entryCount2 ) 
		);
	}
}
