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

#ifndef ICONTROLLABLE_H
#define ICONTROLLABLE_H

#include "model/nifmodel.h"

#include <QObject> // Inherited
#include <QList>
#include <QPersistentModelIndex>


//! @file glcontrollable.h IControllable interface

class Controller;
class Scene;

// A block capable of having a Controller
class IControllable : public QObject
{
	Q_OBJECT

public:
	Scene * const scene;
	const NifFieldConst block;
	const NifModel * const model;

protected:
	QPersistentModelIndex iBlock;
	QList<Controller *> controllers;
	QString name;

public:
	IControllable( Scene * _scene, NifFieldConst _block );
	virtual ~IControllable();

	QModelIndex index() const { return iBlock; } // TODO: Get rid of it
	bool isValid() const { return iBlock.isValid(); }

	auto modelVersion() const { return model->getVersionNumber(); }
	bool modelVersionInRange( quint32 since, quint32 until ) const { return model->checkVersion( since, until ); }
	auto modelBSVersion() const { return model->getBSVersion(); }

	const QString & blockName() const { return name; }

	virtual void clear();

	void update( const NifModel * nif, const QModelIndex & index );
	void update() { update( model, iBlock ); }

	virtual void transform();

	virtual void timeBounds( float & start, float & stop );

	void setSequence( const QString & seqName );

	Controller * findController( const QString & ctrlType, const QString & var1, const QString & var2 ) const;
	Controller * findController( NifFieldConst ctrlBlock ) const;

	static void reportFieldCountMismatch( NifFieldConst rootEntry1, int entryCount1, NifFieldConst rootEntry2, int entryCount2, NifFieldConst reportEntry );
	static void reportFieldCountMismatch( NifFieldConst rootEntry1, NifFieldConst rootEntry2, NifFieldConst reportEntry )
	{
		reportFieldCountMismatch( rootEntry1, rootEntry1.childCount(), rootEntry2, rootEntry2.childCount(), reportEntry );
	}

protected:
	// Create a Controller from a block if applicable
	virtual Controller * createController( NifFieldConst controllerBlock );

	// Actual implementation of update, with the validation check taken care of by update(...)
	virtual void updateImpl( const NifModel * nif, const QModelIndex & index );
};

#endif
