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
#include <QString>


//! @file icontrollable.h IControllable interface

class Controller;
class Scene;
class NifModel;

//! Anything capable of having a Controller
class IControllable : public QObject
{
	Q_OBJECT

	friend class ControllerManager;

public:
	IControllable( Scene * _scene, NifFieldConst _block );
	virtual ~IControllable();

	QModelIndex index() const { return iBlock; }
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

	void setSequence( const QString & seqname );
	Controller * findController( const QString & ctrltype, const QString & var1, const QString & var2 );

	Controller * findController( const QModelIndex & index );

public:
	// scene, block and model below are set once at the creation of a IControllable and never change during its lifetime.
	// At the same time, they are frequently read in a lot of code across NifSkope.
	// That's why they are public but const (could only be set by IControllable::IControllable(...) ).
	// Whatever createas a IControllable, must ensure that it passes a valid non-null scene and a valid block to the constructor.

	Scene * const scene;
	const NifFieldConst block;
	const NifModel * const model;

protected:
	//! Sets the Controller
	virtual void setController( const NifModel * nif, const QModelIndex & iController );

	//! Actual implementation of update, with the validation check taken care of by update(...)
	virtual void updateImpl( const NifModel * nif, const QModelIndex & index );

	QPersistentModelIndex iBlock;

	QList<Controller *> controllers;

	void registerController( const NifModel* nif, Controller *ctrl );

	QString name;
};

#endif
