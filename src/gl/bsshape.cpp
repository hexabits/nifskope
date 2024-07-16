#include "bsshape.h"

#include "gl/glnode.h"
#include "gl/glscene.h"
#include "gl/renderer.h"
#include "io/material.h"
#include "model/nifmodel.h"


void BSShape::updateDataImpl( const NifModel * nif )
{
	NifFieldConst block = nif->block(iBlock);

	isSkinned = block.child("Vertex Desc").value<BSVertexDesc>().HasFlag(VertexAttribute::VA_SKINNING);
	isDynamic = block.inherits("BSDynamicTriShape");
	sRGB      = ( nif->getBSVersion() >= 151 );

	dataBound = BoundSphere(block);

	// Is the shape skinned?
	NifFieldConst skinBlock, skinDataBlock, skinPartBlock;
	if ( isSkinned ) {
		QString skinInstName, skinDataName;
		if ( nif->getBSVersion() >= 130 ) {
			skinInstName = "BSSkin::Instance";
			skinDataName = "BSSkin::BoneData";
		} else {
			skinInstName = "NiSkinInstance";
			skinDataName = "NiSkinData";
		}

		skinBlock = block.child("Skin").linkBlock(skinInstName);
		if ( skinBlock ) {
			iSkin = skinBlock.toIndex(); // ???
			skinDataBlock = skinBlock.child("Data").linkBlock(skinDataName);
			iSkinData = skinDataBlock.toIndex(); // ???
			if ( nif->getBSVersion() == 100 ) {
				skinPartBlock = skinBlock.child("Skin Partition").linkBlock("NiSkinPartition");
				iSkinPart = skinPartBlock.toIndex(); // ???
			}
		}
	}

	// Fill vertex data
	NifFieldConst vertexData;
	if ( skinPartBlock ) {
		// For skinned geometry, the vertex data is stored in the NiSkinPartition
		// The triangles are split up among the partitions
		vertexData = skinPartBlock.child("Vertex Data");
		int dataSize = skinPartBlock.child("Data Size").value<int>();
		int vertexSize = skinPartBlock.child("Vertex Size").value<int>();
		if ( vertexData && dataSize > 0 && vertexSize > 0 )
			numVerts = dataSize / vertexSize;
	} else {
		vertexData = block.child("Vertex Data");
		numVerts = vertexData.childCount();
	}
	iData = vertexData.toIndex(); // ???
	addSelection( vertexData, ShapeSelectionType::BS_VERTEX_DATA );

	TexCoords coordset; // For compatibility with coords list

	QVector<Vector4> dynVerts;
	if ( isDynamic ) {
		auto dynVertsRoot = block["Vertices"];
		addSelection( dynVertsRoot, ShapeSelectionType::VERTICES );
		reportCountMismatch( dynVertsRoot, dynVertsRoot.childCount(), vertexData, numVerts, block );
		dynVerts = dynVertsRoot.array<Vector4>();
		if ( dynVerts.count() < numVerts )
			dynVerts.resize( numVerts );
	}

	if ( numVerts > 0 ) {
		// Pre-cache num. indices of all needed vertex fields to avoid looking them by string name for every vertex in the shape again and again.
		auto firstVertex = vertexData[0];

		int iVertexField     = firstVertex.child("Vertex").row();
		int iNormalField     = firstVertex.child("Normal").row();
		int iTangentField    = firstVertex.child("Tangent").row();
		int iBitangentXField = firstVertex.child("Bitangent X").row();
		int iBitangentYField = firstVertex.child("Bitangent Y").row();
		int iBitangentZField = firstVertex.child("Bitangent Z").row();
		int iUVField         = firstVertex.child("UV").row();
		int iColorField      = firstVertex.child("Vertex Colors").row();

		hasVertexNormals    = ( iNormalField >= 0 );
		hasVertexTangents   = ( iTangentField >= 0 );
		hasVertexBitangents = ( ( iBitangentXField >= 0 || isDynamic ) && ( iBitangentYField >= 0 ) && ( iBitangentZField >= 0 ) );
		hasVertexUVs        = ( iUVField >= 0 );
		hasVertexColors     = ( iColorField >= 0 );

		verts.reserve(numVerts);
		if ( hasVertexNormals )
			norms.reserve(numVerts);
		if ( hasVertexTangents )
			tangents.reserve(numVerts);
		if ( hasVertexBitangents )
			bitangents.reserve(numVerts);
		if ( hasVertexUVs )
			coordset.reserve(numVerts);
		if ( hasVertexColors )
			colors.reserve(numVerts);

		for ( int i = 0; i < numVerts; i++ ) {
			float bitX;
			auto vdata = vertexData[i];

			if ( isDynamic ) {
				auto & dynv = dynVerts.at(i);
				verts << Vector3( dynv );
				bitX = dynv[3];
			} else {
				verts << ( iVertexField >= 0 ? vdata[iVertexField].value<Vector3>() : Vector3() );
				bitX = ( iBitangentXField >= 0 ) ? vdata[iBitangentXField].value<float>() : 0.0f;
			}

			if ( hasVertexNormals )
				norms << vdata[iNormalField].value<ByteVector3>();
			if ( hasVertexTangents )
				tangents << vdata[iTangentField].value<ByteVector3>();
			if ( hasVertexBitangents )
				bitangents << Vector3( bitX, vdata[iBitangentYField].value<float>(), vdata[iBitangentZField].value<float>() );
			if ( hasVertexUVs )
				coordset << vdata[iUVField].value<HalfVector2>();
			if ( hasVertexColors )
				colors << vdata[iColorField].value<ByteColor4>();
		}
	}

	// Add coords as the first set of QList
	coords.append( coordset );

	// Fill triangle (and partition) data
	if ( skinPartBlock ) {
		auto partRoot = skinPartBlock.child("Partitions");
		addSelection( partRoot, ShapeSelectionType::TRIANGLE_ROOT );
		for ( auto partEntry : partRoot.iter() ) {
			TriangleRange * partTriRange = addTriangles( partEntry.child("Triangles") );
			addTriangleRange( partEntry, TriangleRange::FLAG_HIGHLIGHT, partTriRange->start, partTriRange->length );

			auto vertexMapRoot = partEntry.child("Vertex Map");
			if ( vertexMapRoot.childCount() == 0 )
				vertexMapRoot = NifFieldConst();
			addSelection( vertexMapRoot, ShapeSelectionType::VERTICES, vertexMapRoot );
			addSelection( partEntry.child("Vertex Weights"), ShapeSelectionType::VERTICES, vertexMapRoot );
			addSelection( partEntry.child("Bone Indices"), ShapeSelectionType::VERTICES, vertexMapRoot );
			addPartitionBoneSelection( partEntry.child("Bones"), partTriRange );
		}
	} else {
		addTriangles( block.child("Triangles") );
	}

	// Fill skeleton data
	if ( skinBlock ) {
		// skeletonRoot = skinBlock.child("Skeleton Root").link(); 
		skeletonRoot = 0; // Always 0

		if ( nif->getBSVersion() < 130 )
			skeletonTrans = Transform( skinDataBlock );

		initSkinBones( skinBlock.child("Bones"), skinDataBlock.child("Bone List"), block );

		// Read vertex weights from vertex data
		int nBones = bones.count();
		if ( nBones > 0 && numVerts > 0 ) {
			auto firstVertex = vertexData[0];
			int iIndicesField = firstVertex["Bone Indices"].row();
			int iWeightsField = firstVertex["Bone Weights"].row();

			if ( iIndicesField >= 0 && iWeightsField >= 0 ) {
				const int WEIGHTS_PER_VERTEX = 4;
				for ( int vind = 0; vind < numVerts; vind++ ) {
					auto vdata = vertexData[vind];
					auto vbones = vdata[iIndicesField];
					auto vweights = vdata[iWeightsField];
					if ( vbones.childCount() < WEIGHTS_PER_VERTEX || vweights.childCount() < WEIGHTS_PER_VERTEX )
						continue;

					for ( int wind = 0; wind < WEIGHTS_PER_VERTEX; wind++ ) {
						float w = vweights[wind].value<float>();
						if ( w <= 0.0f )
							continue;
						int bind = vbones[wind].value<int>();
						if ( bind >= nBones || bind < 0 ) {
							vbones[wind].reportError( tr("Invalid bone index %1").arg(bind) );
							continue;
						}
						bones[bind].vertexWeights << VertexWeight( vind, w );
					}
				}
			}
		}
	}

	// LODs
	if ( block.isBlockType( "BSMeshLODTriShape" ) ) {
		initLodData( block );
	}

	// Bounding sphere
	addBoundSphereSelection( block.child("Bounding Sphere") );

	// Triangle segments (BSSegmentedTriShape, BSSubIndexTriShape)
	auto segRoot = block.child("Segment");
	addSelection( segRoot, ShapeSelectionType::TRIANGLE_ROOT );
	for ( auto segEntry: segRoot.iter() ) {
		// TODO: validate ranges, with reportError

		const TriangleRange * segRange = addTriangleRange(
			segEntry,
			TriangleRange::FLAG_HIGHLIGHT | TriangleRange::FLAG_DEEP,
			segEntry["Start Index"].value<int>() / 3,
			segEntry["Num Primitives"].value<int>()
		);

		for ( auto subSegEntry : segEntry.child("Sub Segment").iter() ) {
			TriangleRange * subSegRange = addTriangleRange(
				subSegEntry,
				TriangleRange::FLAG_HIGHLIGHT | TriangleRange::FLAG_DEEP,
				subSegEntry["Start Index"].value<int>() / 3,
				subSegEntry["Num Primitives"].value<int>()
			);
			if ( subSegRange )
				subSegRange->parentRange = segRange;
		}
	}

	// BSPackedCombined... blocks
	for ( auto extraLink : block.child("Extra Data List").linkArray() ) {
		auto extraBlock = nif->block( extraLink );
		if ( extraBlock.hasName( "BSPackedCombinedGeomDataExtra", "BSPackedCombinedSharedGeomDataExtra" ) ) {
			for ( auto dataEntry : extraBlock.child("Object Data").iter() ) {
				for ( auto combinedEntry : dataEntry.child("Combined").iter() ) {
					auto pSphere = addBoundSphereSelection( combinedEntry.child("Bounding Sphere") );
					// TODO: copied the code from the previous version of bsshape.cpp, rewrite
					Transform t( combinedEntry.child("Transform") );
					pSphere->absoluteTransform = true;
					pSphere->transform.rotation = t.rotation.inverted();
					pSphere->transform.translation = pSphere->sphere.center;
					pSphere->transform.scale = t.scale;
					pSphere->sphere.center = Vector3();
				}
			}
			iExtraData = extraBlock.toIndex(); // ???
			break;
		}
	}
}

QModelIndex BSShape::vertexAt( int idx ) const
{
	auto nif = NifModel::fromIndex( iBlock );
	if ( !nif )
		return QModelIndex();

	// Vertices are on NiSkinPartition in version 100
	auto blk = iBlock;
	if ( iSkinPart.isValid() ) {
		if ( isDynamic )
			return nif->getIndex( blk, "Vertices" ).child( idx, 0 );

		blk = iSkinPart;
	}

	return nif->getIndex( nif->getIndex( blk, "Vertex Data" ).child( idx, 0 ), "Vertex" );
}

void BSShape::transformShapes()
{
	if ( isHidden() )
		return;

	auto nif = NifModel::fromValidIndex( iBlock );
	if ( !nif ) {
		clear();
		return;
	}

	Node::transformShapes();

	if ( isSkinned && bones.count() && scene->hasOption(Scene::DoSkinning) ) {
		applySkinningTransforms( scene->view );
	} else {
		applyRigidTransforms();
	}

	// Colors
	applyColorTransforms();
}

BoundSphere BSShape::bounds() const
{
	if ( needUpdateBounds ) {
		needUpdateBounds = false;
		if ( verts.count() ) {
			boundSphere = BoundSphere( verts );
		} else {
			boundSphere = dataBound;
		}
	}

	return worldTrans() * boundSphere;
}
