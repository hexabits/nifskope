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

#ifndef GLSHAPE_H
#define GLSHAPE_H

#include "gl/glnode.h" // Inherited
#include "gl/gltools.h"


//! @file glshape.h Shape

class Shape;

// A set of vertices weighted to a bone ( BSSkinBoneTrans )
class SkinBone
{
public:
	Transform transform;
	const Node * node;
	BoundSphere boundSphere;
	QVector<VertexWeight> vertexWeights;

	inline SkinBone()
		: node( nullptr ) {}
	inline SkinBone( NifFieldConst boneDataEntry, const Node * boneNode )
		: transform( boneDataEntry ), node( boneNode ), boundSphere( boneDataEntry ) {}

	Transform localTransform( const Transform & parentTransform, int skeletonRoot ) const;
};


// Base class for shape selection entries
class ShapeSelectionBase
{
	friend class Shape;

public:
	const Shape * shape;
	NifFieldConst rootField;
	NifFieldConst block;
	int level; // The level of rootField in block. For sorting.
	NifFieldConst mapField; // Vertex or bone map field to convert child indices

	ShapeSelectionBase() = delete;
protected:
	ShapeSelectionBase( Shape * shape, NifFieldConst rootField, NifFieldConst mapField );

	virtual bool process( NifFieldConst selectedField, int iSubLevel ) const = 0;
	virtual void postUpdate() {};
	int remapIndex( int i ) const;
};


enum class VertexSelectionType
{
	VERTICES,       // Array of shape's vertices, with each child of rootField representing a vertex
	NORMALS,		// Same as VERTICES, but show also normals
	TANGENTS,		// Same as VERTICES, but show also tangents
	BITANGENTS,		// Same as VERTICES, but show also bitangents
	BS_VERTEX_DATA, // Array of SSE+ vertex data structures
	EXTRA_TANGENTS, // Byte array from extra data block storing tangents and bitangents of the shape
	VERTEX_ROOT,    // If rootField (not its children) is selected, then show all vertices of the shape
};


// A vertex selection
class VertexSelection final : public ShapeSelectionBase
{
	friend class Shape;

public:
	VertexSelectionType type;

	VertexSelection() = delete;
private:
	VertexSelection( Shape * _shape, NifFieldConst _rootField, VertexSelectionType _type, NifFieldConst _mapField )
		: ShapeSelectionBase( _shape, _rootField, _mapField ), type( _type ) {}

protected:
	bool process( NifFieldConst selectedField, int iSubLevel ) const override;
};


// A range of triangles, with start and length (triangle arrays, skin partitions, LOD levels, ...)
class TriangleRange : public ShapeSelectionBase
{
	friend class Shape;

public:
	enum Flags : NifSkopeFlagsType
	{
		// rootField is a simple array of triangles or strip points
		FLAG_ARRAY = 1U << 0,
		// Draw wireframes of all triangles/strips in the shape, with the range highlighted
		FLAG_HIGHLIGHT = 1U << 1,
		// The range is considered selected if any of rootField's children is selected, no matter how deep it is
		FLAG_DEEP = 1U << 2,
	};
	NifSkopeFlagsType flags;
	const TriangleRange * parentRange = nullptr;

	int start;
	int length;
	int realStart = 0;
	int realLength = 0;

	TriangleRange() = delete;
protected:
	TriangleRange( Shape * _shape, NifFieldConst _rootField, NifSkopeFlagsType _flags, int _start, int _length, NifFieldConst _mapField )
		: ShapeSelectionBase( _shape, _rootField, _mapField ), flags( _flags ), start( _start ), length( _length ) {}

public:
	bool isArray() const { return ( flags & FLAG_ARRAY ); }
	bool isHighlight() const { return ( flags & FLAG_HIGHLIGHT ); }
	bool isDeep() const { return ( flags & FLAG_DEEP ); }

	int realEnd() const { return realStart + realLength; }

	virtual const QVector<Triangle> & triangles() const;
	virtual const QVector<int> & triangleMap() const;
	virtual const QVector<Triangle> & otherTriangles() const;

protected:
	void postUpdate() override;
	bool process( NifFieldConst selectedField, int iSubLevel ) const override;
};

inline void glDrawTriangles( const TriangleRange * range )
{
	if ( range )
		glDrawTriangles( range->triangles(), range->realStart, range->realLength );
}


// A selection range for strip triangles of the shape
class StripRange final : public TriangleRange
{
	friend class Shape;

public:
	StripRange() = delete;
private:
	StripRange( Shape * _shape, NifFieldConst _rootField, NifSkopeFlagsType _flags, int _start, int _length, NifFieldConst _mapField )
		: TriangleRange( _shape, _rootField, _flags, _start, _length, _mapField ) {}

public:
	const QVector<Triangle> & triangles() const override;
	const QVector<int> & triangleMap() const override;
	const QVector<Triangle> & otherTriangles() const override;

protected:
	bool process( NifFieldConst selectedField, int iSubLevel ) const override;
};


// Bounding sphere selection
class BoundSphereSelection final : public ShapeSelectionBase
{
	friend class Shape;

public:
	BoundSphere sphere;
	Transform transform;
	bool absoluteTransform = false;

	BoundSphereSelection() = delete;
private:
	BoundSphereSelection( Shape * _shape, NifFieldConst _rootField )
		: ShapeSelectionBase( _shape, _rootField, NifFieldConst() ), sphere( _rootField ) {}

protected:
	bool process( NifFieldConst selectedField, int iSubLevel ) const override;
};


// Skin bone selection
class BoneSelection final : public ShapeSelectionBase
{
	friend class Shape;

public:
	const TriangleRange * triRange;

	BoneSelection() = delete;
private:
	BoneSelection( Shape * _shape, NifFieldConst _rootField, TriangleRange * _triRange, NifFieldConst _boneMapField )
		: ShapeSelectionBase( _shape, _rootField, _boneMapField ), triRange( _triRange ) {}

protected:
	bool process( NifFieldConst selectedField, int iSubLevel ) const override;
};


// Base class for shape nodes
class Shape : public Node
{
	friend class MorphController;
	friend class UVController;
	friend class Renderer;
	friend class ShapeSelectionBase;
	friend class VertexSelection;
	friend class TriangleRange;
	friend class StripRange;
	friend class BoundSphereSelection;
	friend class BoneSelection;

public:
	Shape( Scene * s, const QModelIndex & b );
	virtual ~Shape();

	// IControllable

	void clear() override;
	void transform() override;

	// end IControllable

	void drawShapes( NodeList * secondPass, bool presort ) override;
	void drawSelection() const override;

	QModelIndex vertexAt( int vertexIndex ) const;

	bool isEditorMarker() const;
	bool doSkinning() const;

	void fillViewModeWeights( double * outWeights, bool & outIsSkinned, const int * modeAxes );

protected:
	int shapeNumber;

	void setController( const NifModel * nif, const QModelIndex & controller ) override;
	void updateImpl( const NifModel * nif, const QModelIndex & index ) override;
	void updateData( const NifModel* nif );
	virtual void updateDataImpl( const NifModel* nif ) = 0;

	void reportCountMismatch( NifFieldConst rootEntry1, int entryCount1, NifFieldConst rootEntry2, int entryCount2, NifFieldConst reportEntry ) const;
	void reportCountMismatch( NifFieldConst rootEntry1, NifFieldConst rootEntry2, NifFieldConst reportEntry ) const;

	//! Shape data
	QPersistentModelIndex iData;
	//! Tangent data
	QPersistentModelIndex iExtraData;
	//! Does the data need updating?
	bool needUpdateData = false;

	//! Skin instance
	QPersistentModelIndex iSkin;
	//! Skin data
	QPersistentModelIndex iSkinData;
	//! Skin partition
	QPersistentModelIndex iSkinPart;

	int numVerts = 0;

	//! Vertices
	QVector<Vector3> verts;
	//! Normals
	QVector<Vector3> norms;
	//! Vertex colors
	QVector<Color4> colors;
	//! Tangents
	QVector<Vector3> tangents;
	//! Bitangents
	QVector<Vector3> bitangents;
	//! UV coordinate sets
	QVector<TexCoords> coords;

	QVector<ShapeSelectionBase *> selections;
	NifFieldConst mainVertexRoot;

	VertexSelection * addVertexSelection( NifFieldConst rootField, VertexSelectionType type, NifFieldConst mapField = NifFieldConst() );

	//! Triangles
	QVector<Triangle> triangles;
	//! Map of triangle indices in the shape data to their indices in the QVector
	QVector<int> triangleMap;
	//! Sorted triangles
	QVector<Triangle> sortedTriangles; // TODO: get rid of it

	TriangleRange * addTriangleRange( NifFieldConst rangeRoot, NifSkopeFlagsType rangeFlags, int iStart, int nTris );
	TriangleRange * addTriangleRange( NifFieldConst rangeRoot, NifSkopeFlagsType rangeFlags, int iStart );

	TriangleRange * addTriangles( NifFieldConst rangeRoot, const QVector<Triangle> & tris );
	TriangleRange * addTriangles( NifFieldConst arrayRoot );

	//! Strip points
	QVector<Triangle> stripTriangles;
	QVector<int> stripMap;

	StripRange * addStripRange( NifFieldConst rangeRoot, NifSkopeFlagsType rangeFlags, int iStart, int nStrips, NifFieldConst vertexMapField );
	StripRange * addStripRange( NifFieldConst rangeRoot, NifSkopeFlagsType rangeFlags, int iStart, NifFieldConst vertexMapField  );
	StripRange * addStripRange( NifFieldConst rangeRoot, NifSkopeFlagsType rangeFlags, int iStart );

	StripRange * addStrip( NifFieldConst stripPointsRoot, const QVector<Triangle> & stripTris, NifFieldConst vertexMapField );
	StripRange * addStrips( NifFieldConst stripsRoot, NifSkopeFlagsType rangeFlags );

	BoundSphereSelection * addBoundSphereSelection( NifFieldConst rootField );

	BoneSelection * addBoneSelection( NifFieldConst rootField, TriangleRange * triRange, NifFieldConst boneMapField = NifFieldConst() );
	BoneSelection * addPartitionBoneSelection( NifFieldConst rootField, TriangleRange * triRange );

	//! Is the transform rigid or weighted?
	bool transformRigid = true;
	//! Transformed vertices
	QVector<Vector3> transVerts;
	//! Transformed normals
	QVector<Vector3> transNorms;
	//! Transformed colors (alpha blended)
	QVector<Color4> transColors;
	//! Transformed tangents
	QVector<Vector3> transTangents;
	//! Transformed bitangents
	QVector<Vector3> transBitangents;

	//! Toggle for skinning
	bool isSkinned = false;

	int skeletonRoot = 0;
	Transform skeletonTrans;
	QVector<SkinBone> bones;

	void initSkinBones( NifFieldConst nodeMapRoot, NifFieldConst nodeListRoot, NifFieldConst block );

	void applySkinningTransforms( const Transform & skinTransform );
	void applyRigidTransforms();

	void applyColorTransforms( float alphaBlend = 1.0f );

	void resetBlockData();

	//! Holds the name of the shader, or "" if no shader
	QString shader = "";

	//! Shader property
	BSShaderProperty * bssp = nullptr;
	//! Skyrim shader property
	BSLightingShaderProperty * bslsp = nullptr;
	//! Skyrim effect shader property
	BSEffectShaderProperty * bsesp = nullptr;

	AlphaProperty * alphaProperty = nullptr;

	//! Is shader set to double sided?
	bool isDoubleSided = false;

	bool hasVertexNormals = false;
	bool hasVertexTangents = false;
	bool hasVertexBitangents = false;
	bool hasVertexUVs = false;
	//! Is "Has Vertex Colors" set to Yes
	bool hasVertexColors = false;

	bool sRGB = false;

	bool depthTest = true;
	bool depthWrite = true;
	bool drawInSecondPass = false;
	bool translucent = false;

	void updateShader();

	mutable BoundSphere boundSphere;
	mutable bool needUpdateBounds = false;

	bool isLOD = false;
	QVector<TriangleRange *> lodLevels;
	void initLodData( NifFieldConst block );
	

	// drawSelection helpers
private:

	enum class DrawSelectionMode {
		NO,
		VERTICES,
		VECTORS,
		WIREFRAME,
		BOUND_SPHERE,
	};
	mutable DrawSelectionMode drawSelectionMode = DrawSelectionMode::NO;

	void drawSelection_begin( DrawSelectionMode newMode ) const;
	void drawSelection_end() const;

	void drawSelection_triangles() const;
	// Draw only the triangles from range
	void drawSelection_triangles( const TriangleRange * range ) const;
	// Draw all triangles of the shape, with the triangles from range highlighted with HighlightColor
	void drawSelection_trianglesHighlighted( const TriangleRange * range ) const;
	void drawSelection_triangles( const TriangleRange * partition, int iSelectedTri ) const;

	struct DrawVectorsData
	{
		bool drawNormals;
		bool drawTangents;
		bool drawBitangents;
		float vectorScale;
	};
	bool drawSelection_vectors_init( VertexSelectionType type, DrawVectorsData & outData ) const;
	void drawSelection_vectors( int iStart, int nLength, const DrawVectorsData & drawData ) const;

	void drawSelection_vertices( VertexSelectionType type ) const;
	void drawSelection_vertices( VertexSelectionType type, int iSelectedVertex ) const;

	void drawSelection_sphere( const BoundSphere & sphere, const Transform & transform, bool hightlightCenter ) const;
	void drawSelection_boundSphere( const BoundSphereSelection * selSphere, bool hightlightCenter ) const;
	void drawSelection_bone( const BoneSelection * selection, int iSelectedBone, bool drawBoundSphere, bool hightlightSphereCenter ) const;
};


// Inlines

inline Transform SkinBone::localTransform( const Transform & parentTransform, int skeletonRoot ) const
{
	if ( node )
		return parentTransform * node->localTrans( skeletonRoot );
	return parentTransform;
}

inline void Shape::reportCountMismatch( NifFieldConst rootEntry1, NifFieldConst rootEntry2, NifFieldConst reportEntry ) const
{
	reportCountMismatch( rootEntry1, rootEntry1.childCount(), rootEntry2, rootEntry2.childCount(), reportEntry );
}

inline void Shape::drawSelection_end() const
{
	drawSelection_begin( DrawSelectionMode::NO );
}

inline VertexSelection * Shape::addVertexSelection( NifFieldConst rootField, VertexSelectionType type, NifFieldConst mapField )
{
	return rootField ? new VertexSelection( this, rootField, type, mapField ) : nullptr;
}

inline TriangleRange * Shape::addTriangleRange( NifFieldConst rangeRoot, NifSkopeFlagsType rangeFlags, int iStart, int nTris )
{
	return new TriangleRange( this, rangeRoot, rangeFlags, iStart, nTris, NifFieldConst() );
}

inline TriangleRange * Shape::addTriangleRange( NifFieldConst rangeRoot, NifSkopeFlagsType rangeFlags, int iStart )
{
	return addTriangleRange( rangeRoot, rangeFlags, iStart, triangles.count() - iStart );
}

inline TriangleRange * Shape::addTriangles( NifFieldConst arrayRoot )
{
	return arrayRoot ? addTriangles( arrayRoot, arrayRoot.array<Triangle>() ) : nullptr;
}

inline StripRange * Shape::addStripRange( NifFieldConst rangeRoot, NifSkopeFlagsType rangeFlags, int iStart, int nStrips, NifFieldConst vertexMapField )
{
	return new StripRange( this, rangeRoot, rangeFlags, iStart, nStrips, vertexMapField );
}

inline StripRange * Shape::addStripRange( NifFieldConst rangeRoot, NifSkopeFlagsType rangeFlags, int iStart, NifFieldConst vertexMapField )
{
	return addStripRange( rangeRoot, rangeFlags, iStart, stripTriangles.count() - iStart, vertexMapField );
}

inline StripRange * Shape::addStripRange( NifFieldConst rangeRoot, NifSkopeFlagsType rangeFlags, int iStart )
{
	return addStripRange( rangeRoot, rangeFlags, iStart, NifFieldConst() );
}

inline BoundSphereSelection * Shape::addBoundSphereSelection( NifFieldConst rootField )
{
	return rootField ? new BoundSphereSelection( this, rootField ) : nullptr;
}

inline BoneSelection * Shape::addBoneSelection( NifFieldConst rootField, TriangleRange * triRange, NifFieldConst boneMapField )
{
	return rootField ? new BoneSelection( this, rootField, triRange, boneMapField ) : nullptr;
}

inline BoneSelection * Shape::addPartitionBoneSelection( NifFieldConst rootField, TriangleRange * triRange )
{
	return addBoneSelection( rootField, triRange, rootField );
}

#endif
