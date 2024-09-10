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

#ifndef GLNODE_H
#define GLNODE_H

#include "gl/glcontrollable.h" // Inherited
#include "gl/glproperty.h"

#include <QList>
#include <QPersistentModelIndex>
#include <QPointer>


//! @file glnode.h Node, NodeList

class Node;
class NifModel;

// A list of Nodes without duplicates and with reference counting
class NodeList final
{
public:
	NodeList() {}
	NodeList( const NodeList & other ) { operator=( other ); }
	~NodeList();

	void clear();

	NodeList & operator=( const NodeList & other );

	void add( Node * node );
	void del( Node * node );
	bool has( Node * node ) const { return nodes.contains( node ); }

	void validate();

	const QVector<Node *> & list() const { return nodes; }

	Node * get( NifFieldConst nodeBlock ) const;

	void sort();
	void alphaSort();

private:
	QVector<Node *> nodes;

	void attach( Node * node );
	void detach( Node * node );
};


class Node : public IControllable
{
	friend class ControllerManager;
	friend class NodeList;
	friend class LODNode;

	// Interpolator managers
	friend class TransformInterpolator;
	friend class BSplineInterpolator;
	friend class VisibilityInterpolator;

	typedef union
	{
		quint16 bits;

		struct Node
		{
			bool hidden : 1;
		} node;
	} NodeFlags;

public:
	Node( Scene * _scene, NifFieldConst _block );

	static int SELECTING;

	int id() const { return nodeId; }

	// IControllable

	void clear() override;
	void transform() override;

	// end IControllable

	virtual void transformShapes();

	virtual void draw();
	virtual void drawShapes( NodeList * secondPass = nullptr, bool presort = false );
	virtual void drawHavok();
	virtual void drawFurn();
	virtual void drawSelection() const;

	virtual float viewDepth() const;
	virtual class BoundSphere bounds() const;
	virtual const Vector3 center() const;
	virtual const Transform & viewTrans() const;
	virtual const Transform & worldTrans() const;
	virtual const Transform & localTrans() const { return local; }
	virtual Transform localTrans( int parentNode ) const;

	virtual bool isHidden() const;
	virtual QString textStats() const;

	bool isVisible() const { return !isHidden(); }
	bool isPresorted() const { return presorted; }
	
	Node * findChild( int id ) const;
	Node * findChild( const QString & str ) const;

	Node * findParent( int id ) const;
	Node * parentNode() const { return parent; }
	void makeParent( Node * parent );

	template <typename T> T * findProperty() const;
	void activeProperties( PropertyList & list ) const;

	Controller * findPropertyController( const QString & propType, const QString & ctrlType, const QString & var1, const QString & var2 ) const;
	Controller * findPropertyController( const QString & propType, NifFieldConst ctrlBlock ) const;

public slots:
	void updateSettings();

protected:
	Controller * createController( NifFieldConst controllerBlock ) override;
	void updateImpl( const NifModel * nif, const QModelIndex & block ) override;

	// Old Options API
	//	TODO: Move away from the GL-like naming
	void glHighlightColor() const;
	void glNormalColor() const;

	QPointer<Node> parent;
	NodeList children;

	PropertyList properties;

	Transform local;

	NodeFlags flags;

	struct Settings
	{
		QColor highlight;
		QColor wireframe;
	} cfg;


	bool presorted = false;

	int nodeId;
	int ref;
};

template <typename T> inline T * Node::findProperty() const
{
	T * prop = properties.get<T>();

	if ( prop )
		return prop;

	if ( parent )
		return parent->findProperty<T>();

	return nullptr;
}

//! A Node with levels of detail
class LODNode : public Node
{
public:
	LODNode( Scene * _scene, NifFieldConst _block );

	// IControllable

	void clear() override;
	void transform() override;

	// end IControllable

protected:
	QList<QPair<float, float> > ranges;
	QPersistentModelIndex iData;

	Vector3 center;

	void updateImpl( const NifModel * nif, const QModelIndex & block ) override;
};

//! A Node that always faces the camera
class BillboardNode : public Node
{
public:
	BillboardNode( Scene * _scene, NifFieldConst _block );

	const Transform & viewTrans() const override;
};


// Inlines - NodeList

inline void NodeList::attach( Node * node )
{
	node->ref++;
	nodes.append( node );
}

inline void NodeList::detach( Node * node )
{
	Q_ASSERT( node->ref > 0 );
	if ( --node->ref <= 0 )
		delete node;
}

#endif
