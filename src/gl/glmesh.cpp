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

#include "glmesh.h"

#include "message.h"
#include "gl/controllers.h"
#include "gl/glscene.h"
#include "gl/renderer.h"
#include "io/material.h"
#include "io/nifstream.h"
#include "model/nifmodel.h"

#include <QBuffer>
#include <QDebug>
#include <QSettings>

#include <QOpenGLFunctions>


//! @file glmesh.cpp Scene management for visible meshes such as NiTriShapes.

const char * NIMESH_ABORT = QT_TR_NOOP( "NiMesh rendering encountered unsupported types. Rendering may be broken." );

void Mesh::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Shape::updateImpl(nif, index);

	if ( index == iBlock ) {
		isLOD = nif->isNiBlock( iBlock, "BSLODTriShape" );
		if ( isLOD )
			emit nif->lodSliderChanged( true );

	} else if ( index == iData || index == iTangentData ) {
		needUpdateData = true;

	}
}

void Mesh::updateDataImpl( const NifModel * nif )
{
	if ( nif->checkVersion( 0x14050000, 0 ) && nif->blockInherits( iBlock, "NiMesh" ) )
		updateData_NiMesh( nif );
	else
		updateData_NiTriShape( nif );
}

void Mesh::updateData_NiMesh( const NifModel * nif )
{
	auto block = nif->field(iBlock);
	auto datastreams = block.child("Datastreams");
	if ( !datastreams )
		return;
	iData = datastreams.toIndex(); // ???
	int nTotalStreams = datastreams.childCount();
			
	// All the semantics used by this mesh
	NiMesh::SemanticFlags semFlags = NiMesh::HAS_NONE;

	// Loop over the data once for initial setup and validation
	// and build the semantic-index maps for each datastream's components
	using CompSemIdxMap = QVector<QPair<NiMesh::Semantic, uint>>;
	QVector<CompSemIdxMap> compSemanticIndexMaps;
	for ( int i = 0; i < nTotalStreams; i++ ) {
		auto streamEntry = datastreams[i];

		auto streamBlock = streamEntry.child("Stream").linkBlock();
		auto usage = NiMesh::DataStreamUsage( streamBlock.child("Usage").value<uint>() );
		auto access = streamBlock.child("Access").value<uint>();
		// Invalid Usage and Access, abort
		if ( usage == access && access == 0 )
			return;

		// For each datastream, store the semantic and the index (used for E_TEXCOORD)
		auto componentSemantics = streamEntry["Component Semantics"];
		auto numComponents = streamEntry["Num Components"].value<uint>();
		CompSemIdxMap compSemanticIndexMap;
		for ( uint j = 0; j < numComponents; j++ ) {
			auto componentEntry = componentSemantics[j];
			auto entrySemantic = NiMesh::semanticStrings.value( componentEntry["Name"].value<QString>() );
			auto entryIndex = componentEntry["Index"].value<uint>();
			compSemanticIndexMap.insert( j, {entrySemantic, entryIndex} );

			// Create UV stubs for multi-coord systems
			if ( entrySemantic == NiMesh::E_TEXCOORD )
				coords.append( TexCoords() );

			// Assure Index datastream is first and Usage is correct
			bool invalidIndex = false;
			if ( (entrySemantic == NiMesh::E_INDEX && (i != 0 || usage != NiMesh::USAGE_VERTEX_INDEX))
					|| (usage == NiMesh::USAGE_VERTEX_INDEX && (i != 0 || entrySemantic != NiMesh::E_INDEX)) )
				invalidIndex = true;

			if ( invalidIndex ) {
				streamEntry.reportError( tr( "NifSkope requires 'INDEX' datastream be first, with Usage type 'USAGE_VERTEX_INDEX'." ) );
				return;
			}

			semFlags = NiMesh::SemanticFlags(semFlags | (1 << entrySemantic));
		}

		compSemanticIndexMaps << compSemanticIndexMap;
	}

	// This NiMesh does not have vertices, abort
	if ( !(semFlags & NiMesh::HAS_POSITION || semFlags & NiMesh::HAS_POSITION_BP) )
		return;

	// The number of triangle indices across the submeshes for this NiMesh
	quint32 totalIndices = 0;
	QVector<quint16> indices;
	// The highest triangle index value in this NiMesh
	quint32 maxIndex = 0;
	// The Nth component after ignoring DataStreamUsage > 1
	int compIdx = 0;
	for ( int i = 0; i < nTotalStreams; i++ ) {
		// TODO: For now, submeshes are not actually used and the regions are 
		// filled in order for each data stream.
		// Submeshes may be required if total index values exceed USHRT_MAX
		auto streamEntry = datastreams[i];

		QMap<ushort, ushort> submeshMap;
		auto numSubmeshes = streamEntry.child("Num Submeshes").value<ushort>();
		auto submeshMapEntries = streamEntry.child("Submesh To Region Map");
		for ( ushort j = 0; j < numSubmeshes; j++ )
			submeshMap.insert( j, submeshMapEntries[j].value<ushort>() );

		// Get the datastream
		auto streamBlock = streamEntry.child("Stream").linkBlock();

		auto usage = NiMesh::DataStreamUsage( streamBlock.child("Usage").value<uint>() );
		// Only process USAGE_VERTEX and USAGE_VERTEX_INDEX
		if ( usage > NiMesh::USAGE_VERTEX )
			continue;

		// Datastream can be split into multiple regions
		// Each region has a Start Index which is added as an offset to the index read from the stream
		QVector<QPair<quint32, quint32>> regions;
		quint32 numIndices = 0;
		auto regionEntries = streamBlock.child("Regions");
		if ( regionEntries ) {
			quint32 numRegions = streamBlock["Num Regions"].value<quint32>();
			for ( quint32 j = 0; j < numRegions; j++ ) {
				auto entry = regionEntries[j];
				regions.append( { entry["Start Index"].value<quint32>(), entry["Num Indices"].value<quint32>() } );

				numIndices += regions[j].second;
			}
		}

		if ( usage == NiMesh::USAGE_VERTEX_INDEX ) {
			totalIndices = numIndices;
			// RESERVE not RESIZE
			indices.reserve( totalIndices );
		} else if ( compIdx == 1 ) {
			// Indices should be built already
			if ( indices.size() != totalIndices )
				return;

			quint32 maxSize = maxIndex + 1;
			// RESIZE
			verts.resize( maxSize );
			norms.resize( maxSize );
			tangents.resize( maxSize );
			bitangents.resize( maxSize );
			colors.resize( maxSize );
			weights.resize( maxSize );
			if ( coords.size() == 0 )
				coords.resize( 1 );

			for ( auto & c : coords )
				c.resize( maxSize );
		}

		// Get the format of each component
		QVector<NiMesh::DataStreamFormat> datastreamFormats;
		auto numComponents = streamBlock["Num Components"].value<uint>();
		auto componentFormats = streamBlock["Component Formats"];
		for ( uint j = 0; j < numComponents; j++ ) {
			auto format = componentFormats[j].value<uint>();
			datastreamFormats.append( NiMesh::DataStreamFormat(format) );
		}

		Q_ASSERT( compSemanticIndexMaps[i].size() == numComponents );

		auto tempMdl = std::make_unique<NifModel>( this );

		QByteArray streamData = streamBlock["Data"][0].value<QByteArray>();
		QBuffer streamBuffer( &streamData );
		streamBuffer.open( QIODevice::ReadOnly );

		NifIStream tempInput( tempMdl.get(), &streamBuffer );
		NifValue tempValue;

		bool abort = false;
		for ( const auto & r : regions ) for ( uint j = 0; j < r.second; j++ ) {
			auto off = r.first;
			Q_ASSERT( totalIndices >= off + j );
			for ( uint k = 0; k < numComponents; k++ ) {
				auto typeK = datastreamFormats[k];
				int typeLength = ( (typeK & 0x000F0000) >> 0x10 );

				switch ( (typeK & 0x00000FF0) >> 0x04 ) {
				case 0x10:
					tempValue.changeType( NifValue::tByte );
					break;
				case 0x11:
					if ( typeK == NiMesh::F_NORMUINT8_4 )
						tempValue.changeType( NifValue::tByteColor4 );
					typeLength = 1;
					break;
				case 0x13:
					if ( typeK == NiMesh::F_NORMUINT8_4_BGRA )
						tempValue.changeType( NifValue::tByteColor4 );
					typeLength = 1;
					break;
				case 0x21:
					tempValue.changeType( NifValue::tShort );
					break;
				case 0x23:
					if ( typeLength == 3 )
						tempValue.changeType( NifValue::tHalfVector3 );
					else if ( typeLength == 2 )
						tempValue.changeType( NifValue::tHalfVector2 );
					else if ( typeLength == 1 )
						tempValue.changeType( NifValue::tHfloat );

					typeLength = 1;

					break;
				case 0x42:
					tempValue.changeType( NifValue::tInt );
					break;
				case 0x43:
					if ( typeLength == 3 )
						tempValue.changeType( NifValue::tVector3 );
					else if ( typeLength == 2 )
						tempValue.changeType( NifValue::tVector2 );
					else if ( typeLength == 4 )
						tempValue.changeType( NifValue::tVector4 );
					else if ( typeLength == 1 )
						tempValue.changeType( NifValue::tFloat );

					typeLength = 1;

					break;
				}

				for ( int l = 0; l < typeLength; l++ ) {
					tempInput.read( tempValue );
				}

				auto compType = compSemanticIndexMaps[i].value( k ).first;
				switch ( typeK )
				{
				case NiMesh::F_FLOAT32_3:
				case NiMesh::F_FLOAT16_3:
					Q_ASSERT( usage == NiMesh::USAGE_VERTEX );
					switch ( compType ) {
					case NiMesh::E_POSITION:
					case NiMesh::E_POSITION_BP:
						verts[j + off] = tempValue.get<Vector3>( nif, nullptr );
						break;
					case NiMesh::E_NORMAL:
					case NiMesh::E_NORMAL_BP:
						norms[j + off] = tempValue.get<Vector3>( nif, nullptr );
						break;
					case NiMesh::E_TANGENT:
					case NiMesh::E_TANGENT_BP:
						tangents[j + off] = tempValue.get<Vector3>( nif, nullptr );
						break;
					case NiMesh::E_BINORMAL:
					case NiMesh::E_BINORMAL_BP:
						bitangents[j + off] = tempValue.get<Vector3>( nif, nullptr );
						break;
					default:
						break;
					}
					break;
				case NiMesh::F_UINT16_1:
					if ( compType == NiMesh::E_INDEX ) {
						Q_ASSERT( usage == NiMesh::USAGE_VERTEX_INDEX );
						// TODO: The total index value across all submeshes
						// is likely allowed to exceed USHRT_MAX.
						// For now limit the index.
						quint32 ind = tempValue.get<quint16>( nif, nullptr ) + off;
						if ( ind > 0xFFFF )
							qDebug() << QString( "[%1] %2" ).arg( streamBlock.repr() ).arg( ind );

						ind = std::min( ind, (quint32)0xFFFF );

						// Store the highest index
						if ( ind > maxIndex )
							maxIndex = ind;

						indices.append( (quint16)ind );
					}
					break;
				case NiMesh::F_FLOAT32_2:
				case NiMesh::F_FLOAT16_2:
					Q_ASSERT( usage == NiMesh::USAGE_VERTEX );
					if ( compType == NiMesh::E_TEXCOORD ) {
						quint32 coordSet = compSemanticIndexMaps[i].value( k ).second;
						Q_ASSERT( coords.size() > coordSet );
						coords[coordSet][j + off] = tempValue.get<Vector2>( nif, nullptr );
					}
					break;
				case NiMesh::F_UINT8_4:
					// BLENDINDICES, do nothing for now
					break;
				case NiMesh::F_NORMUINT8_4:
					Q_ASSERT( usage == NiMesh::USAGE_VERTEX );
					if ( compType == NiMesh::E_COLOR )
						colors[j + off] = tempValue.get<ByteColor4>( nif, nullptr );
					break;
				case NiMesh::F_NORMUINT8_4_BGRA:
					Q_ASSERT( usage == NiMesh::USAGE_VERTEX );
					if ( compType == NiMesh::E_COLOR ) {
						// Swizzle BGRA -> RGBA
						auto c = tempValue.get<ByteColor4>( nif, nullptr ).data();
						colors[j + off] = {c[2], c[1], c[0], c[3]};
					}
					break;
				default:
					streamBlock.reportError( tr( "Unsupported Component: %2" ).arg( NifValue::enumOptionName( "ComponentFormat", typeK ) ) );
					abort = true;
					break;
				}

				if ( abort == true )
					break;
			}
		}

		// Clear is extremely expensive. Must be outside of loop
		tempMdl->clear();

		compIdx++;
	}

	// Set vertex attributes flags
	if ( semFlags & NiMesh::HAS_NORMAL )
		hasVertexNormals = true;
	if ( semFlags & NiMesh::HAS_TANGENT )
		hasVertexTangents = true;
	if ( semFlags & NiMesh::HAS_BINORMAL )
		hasVertexBitangents = true;
	if ( semFlags & NiMesh::HAS_TEXCOORD )
		hasVertexUVs = true;
	if ( semFlags & NiMesh::HAS_COLOR )
		hasVertexColors = true;
	if ( !(semFlags & NiMesh::HAS_BLENDINDICES) || !(semFlags & NiMesh::HAS_BLENDWEIGHT) )
		weights.clear();

	Q_ASSERT( verts.size() == maxIndex + 1 );
	Q_ASSERT( indices.size() == totalIndices );
	numVerts = verts.count();

	// Make geometry
	triangles.resize( indices.size() / 3 );
	auto typeField = block["Primitive Type"];
	auto meshPrimitiveType = typeField.value<uint>();
	switch ( meshPrimitiveType ) {
	case NiMesh::PRIMITIVE_TRIANGLES:
		for ( int k = 0, t = 0; k < indices.size(); k += 3, t++ )
			triangles[t] = { indices[k], indices[k + 1], indices[k + 2] };
		break;
	case NiMesh::PRIMITIVE_TRISTRIPS:
	case NiMesh::PRIMITIVE_LINES:
	case NiMesh::PRIMITIVE_LINESTRIPS:
	case NiMesh::PRIMITIVE_QUADS:
	case NiMesh::PRIMITIVE_POINTS:
		typeField.reportError( tr("Unsupported primitive type value: %1").arg( NifValue::enumOptionName("MeshPrimitiveType", meshPrimitiveType) ) );
		break;
	}
}

void Mesh::updateData_NiTriShape( const NifModel * nif )
{
	// Find iData and iSkin blocks among the children
	NifFieldConst dataBlock, skinBlock;
	for ( auto childLink : nif->getChildLinks( id() ) ) {
		auto childBlock = nif->block( childLink );
		if ( !childBlock )
			continue;

		if ( childBlock.inherits( "NiTriShapeData", "NiTriStripsData" ) ) {
			if ( !dataBlock )
				dataBlock = childBlock;
			else if ( dataBlock != childBlock )
				nif->block(iBlock).reportError( tr( "Block has multiple data blocks" ) );
		} else if ( childBlock.inherits( "NiSkinInstance" ) ) {
			if ( !skinBlock )
				skinBlock = childBlock;
			else if ( skinBlock != childBlock )
				nif->block(iBlock).reportError( tr( "Block has multiple skin instances" ) );
		}
	}
	if ( !dataBlock )
		return;
	iData = dataBlock.toIndex(); // ???

	// Fill vertex data
	verts = dataBlock.child("Vertices").array<Vector3>();
	numVerts = verts.count();

	auto normalsField = dataBlock.child("Normals");
	if ( normalsField ) {
		hasVertexNormals = true;
		norms = normalsField.array<Vector3>();
	}

	auto tangentsField = dataBlock.child("Tangents");
	if ( tangentsField ) {
		hasVertexTangents = true;
		tangents = tangentsField.array<Vector3>();
	}

	auto bitangentsField = dataBlock.child("Bitangents");
	if ( bitangentsField ) {
		hasVertexBitangents = true;
		bitangents = bitangentsField.array<Vector3>();
	}

	for ( auto extraLink : nif->block(iBlock).child("Extra Data List").linkArray() ) {
		auto extraBlock = nif->block(extraLink);
		if ( extraBlock.inherits("NiBinaryExtraData") && extraBlock.child("Name").value<QString>() == QLatin1String("Tangent space (binormal & tangent vectors)") ) {
			hasVertexTangents = true;
			hasVertexBitangents = true;
			iTangentData = extraBlock.toIndex(); // ???
			QByteArray extraData = extraBlock.child("Binary Data").value<QByteArray>();
			int nExtraCount = extraData.count() / ( sizeof(Vector3) * 2 );
			tangents.resize(nExtraCount);
			bitangents.resize(nExtraCount);
			Vector3 * t = (Vector3 *) extraData.data();
			for ( int i = 0; i < nExtraCount; i++ )
				tangents[i] = *t++;
			for ( int i = 0; i < nExtraCount; i++ )
				bitangents[i] = *t++;
		}
	}

	auto uvSets = dataBlock.child("UV Sets");
	if ( uvSets ) {
		hasVertexUVs = true;
		for ( auto uvSet : uvSets.iter() )
			coords.append( uvSet.array<Vector2>() );
	}

	auto colorsField = dataBlock.child("Vertex Colors");
	if ( colorsField ) {
		hasVertexColors = true;
		colors = colorsField.array<Color4>();
	}
	
	// Fill triangle/strips data
	if ( dataBlock.isBlockType("NiTriShapeData") ) {
		triangles = dataBlock.child("Triangles").array<Triangle>();
	} else if ( dataBlock.isBlockType("NiTriStripsData") ) {
		auto stripPoints = dataBlock.child("Points");
		if ( stripPoints ) {
			for ( auto ps : stripPoints.iter() )
				tristrips.append( ps.array<quint16>() );
		} else {
			dataBlock.reportError( tr("Invalid 'Points' array") );
		}
	}

	// Fill skinning and skeleton data
	if ( skinBlock ) {
		isSkinned = true;
		iSkin = skinBlock.toIndex(); // ???

		auto skinDataBlock = skinBlock.child("Data").linkBlock("NiSkinData");
		iSkinData = skinDataBlock.toIndex(); // ???

		auto skinPartBlock = skinBlock.child("Skin Partition").linkBlock("NiSkinPartition");
		if ( !skinPartBlock && skinDataBlock ) {
			// nif versions < 10.2.0.0 have skin partition linked in the skin data block
			skinPartBlock = skinDataBlock.child("Skin Partition").linkBlock("NiSkinPartition");
		}
		iSkinPart = skinPartBlock.toIndex(); // ???

		skeletonRoot = skinBlock.child("Skeleton Root").link();
		
		skeletonTrans = Transform( skinDataBlock );

		bones = skinBlock.child("Bones").linkArray();

		auto boneList = skinDataBlock.child("Bone List");
		if ( boneList ) {
			int nTotalBones = bones.count();
			int nBoneList = boneList.childCount();
			if ( nBoneList > nTotalBones )
				nBoneList = nTotalBones;
			// Ignore weights listed in NiSkinData if NiSkinPartition exists
			int vcnt = ( skinDataBlock.child("Has Vertex Weights").value<unsigned char>() && !skinPartBlock ) ? numVerts : 0;
			for ( int i = 0; i < nBoneList; i++ )
				weights.append( BoneWeights( boneList[i], bones[i], vcnt ) );
		}

		if ( skinPartBlock ) {
			auto partitionEntries = skinPartBlock.child("Partitions");

			uint numTris = 0;
			uint numStrips = 0;
			partitions.reserve( partitionEntries.childCount() );
			for ( auto pentry : partitionEntries.iter() ) {
				auto p = SkinPartition( pentry );
				partitions.append( p );
				numTris += p.triangles.size();
				numStrips += p.tristrips.size();
			}

			triangles.clear();
			tristrips.clear();

			triangles.reserve( numTris );
			tristrips.reserve( numStrips );

			for ( const SkinPartition& part : partitions ) {
				triangles << part.getRemappedTriangles();
				tristrips << part.getRemappedTristrips();
			}
		}
	}
}

QModelIndex Mesh::vertexAt( int idx ) const
{
	auto nif = NifModel::fromIndex( iBlock );
	if ( !nif )
		return QModelIndex();

	auto iVertexData = nif->getIndex( iData, "Vertices" );
	auto iVertex = iVertexData.child( idx, 0 );

	return iVertex;
}

bool compareTriangles( const QPair<int, float> & tri1, const QPair<int, float> & tri2 )
{
	return ( tri1.second < tri2.second );
}

void Mesh::transformShapes()
{
	if ( isHidden() )
		return;

	Node::transformShapes();

	transformRigid = true;

	if ( isSkinned && ( weights.count() || partitions.count() ) && scene->hasOption(Scene::DoSkinning) ) {
		transformRigid = false;

		int vcnt = verts.count();
		int ncnt = norms.count();
		int tcnt = tangents.count();
		int bcnt = bitangents.count();

		transVerts.resize( vcnt );
		transVerts.fill( Vector3() );
		transNorms.resize( vcnt );
		transNorms.fill( Vector3() );
		transTangents.resize( vcnt );
		transTangents.fill( Vector3() );
		transBitangents.resize( vcnt );
		transBitangents.fill( Vector3() );

		Node * root = findParent( skeletonRoot );

		if ( partitions.count() ) {
			for ( const SkinPartition& part : partitions ) {
				QVector<Transform> boneTrans( part.boneMap.count() );

				for ( int t = 0; t < boneTrans.count(); t++ ) {
					Node * bone = root ? root->findChild( bones.value( part.boneMap[t] ) ) : 0;
					boneTrans[ t ] = scene->view;

					if ( bone )
						boneTrans[ t ] = boneTrans[ t ] * bone->localTrans( skeletonRoot ) * weights.value( part.boneMap[t] ).trans;

					//if ( bone ) boneTrans[ t ] = bone->viewTrans() * weights.value( part.boneMap[t] ).trans;
				}

				for ( int v = 0; v < part.vertexMap.count(); v++ ) {
					int vindex = part.vertexMap[ v ];
					if ( vindex < 0 || vindex >= vcnt )
						break;

					if ( transVerts[vindex] == Vector3() ) {
						for ( int w = 0; w < part.numWeightsPerVertex; w++ ) {
							QPair<int, float> weight = part.weights[ v * part.numWeightsPerVertex + w ];


							Transform trans = boneTrans.value( weight.first );

							if ( vcnt > vindex )
								transVerts[vindex] += trans * verts[vindex] * weight.second;
							if ( ncnt > vindex )
								transNorms[vindex] += trans.rotation * norms[vindex] * weight.second;
							if ( tcnt > vindex )
								transTangents[vindex] += trans.rotation * tangents[vindex] * weight.second;
							if ( bcnt > vindex )
								transBitangents[vindex] += trans.rotation * bitangents[vindex] * weight.second;
						}
					}
				}
			}
		} else {
			int x = 0;
			for ( const BoneWeights& bw : weights ) {
				Transform trans = viewTrans() * skeletonTrans;
				Node * bone = root ? root->findChild( bw.bone ) : 0;

				if ( bone )
					trans = trans * bone->localTrans( skeletonRoot ) * bw.trans;

				if ( bone )
					weights[x++].tcenter = bone->viewTrans() * bw.center;
				else
					x++;

				for ( const VertexWeight& vw : bw.weights ) {
					int vindex = vw.vertex;
					if ( vindex < 0 || vindex >= vcnt )
						break;

					if ( vcnt > vindex )
						transVerts[vindex] += trans * verts[vindex] * vw.weight;
					if ( ncnt > vindex )
						transNorms[vindex] += trans.rotation * norms[vindex] * vw.weight;
					if ( tcnt > vindex )
						transTangents[vindex] += trans.rotation * tangents[vindex] * vw.weight;
					if ( bcnt > vindex )
						transBitangents[vindex] += trans.rotation * bitangents[vindex] * vw.weight;
				}
			}
		}

		for ( int n = 0; n < transNorms.count(); n++ )
			transNorms[n].normalize();

		for ( int t = 0; t < transTangents.count(); t++ )
			transTangents[t].normalize();

		for ( int t = 0; t < transBitangents.count(); t++ )
			transBitangents[t].normalize();

		boundSphere = BoundSphere( transVerts );
		boundSphere.applyInv( viewTrans() );
		needUpdateBounds = false;
	} else {
		transVerts = verts;
		transNorms = norms;
		transTangents = tangents;
		transBitangents = bitangents;
		transColors = colors;
	}

	sortedTriangles = triangles;

	MaterialProperty * matprop = findProperty<MaterialProperty>();
	if ( matprop && matprop->alphaValue() != 1.0 ) {
		float a = matprop->alphaValue();
		transColors.resize( colors.count() );

		for ( int c = 0; c < colors.count(); c++ )
			transColors[c] = colors[c].blend( a );
	} else {
		transColors = colors;
		// TODO (Gavrant): suspicious code. Should the check be replaced with !bssp.hasVertexAlpha ?
		if ( bslsp && !bslsp->hasSF1(ShaderFlags::SLSF1_Vertex_Alpha) ) {
			for ( int c = 0; c < colors.count(); c++ )
				transColors[c] = Color4( colors[c].red(), colors[c].green(), colors[c].blue(), 1.0f );
		}
	}
}

BoundSphere Mesh::bounds() const
{
	if ( needUpdateBounds ) {
		needUpdateBounds = false;
		boundSphere = BoundSphere( verts );
	}

	return worldTrans() * boundSphere;
}

void Mesh::drawShapes( NodeList * secondPass, bool presort )
{
	if ( isHidden() )
		return;

	// TODO: Only run this if BSXFlags has "EditorMarkers present" flag
	if ( !scene->hasOption(Scene::ShowMarkers) && name.startsWith( "EditorMarker" ) )
		return;

	// BSOrderedNode
	presorted |= presort;

	// Draw translucent meshes in second pass
	if ( secondPass && drawInSecondPass ) {
		secondPass->add( this );
		return;
	}

	auto nif = NifModel::fromIndex( iBlock );
	
	if ( Node::SELECTING ) {
		if ( scene->isSelModeObject() ) {
			int s_nodeId = ID2COLORKEY( nodeId );
			glColor4ubv( (GLubyte *)&s_nodeId );
		} else {
			glColor4f( 0, 0, 0, 1 );
		}
	}

	// TODO: Option to hide Refraction and other post effects

	// rigid mesh? then pass the transformation on to the gl layer

	if ( transformRigid ) {
		glPushMatrix();
		glMultMatrix( viewTrans() );
	}

	//if ( !Node::SELECTING ) {
	//	qDebug() << viewTrans().translation;
		//qDebug() << Vector3( nif->get<Vector4>( iBlock, "Translation" ) );
	//}

	// Debug axes
	//drawAxes(Vector3(), 35.0);

	// setup array pointers

	// Render polygon fill slightly behind alpha transparency and wireframe
	glEnable( GL_POLYGON_OFFSET_FILL );
	if ( drawInSecondPass )
		glPolygonOffset( 0.5f, 1.0f );
	else
		glPolygonOffset( 1.0f, 2.0f );

	glEnableClientState( GL_VERTEX_ARRAY );
	glVertexPointer( 3, GL_FLOAT, 0, transVerts.constData() );

	if ( !Node::SELECTING ) {
		if ( transNorms.count() ) {
			glEnableClientState( GL_NORMAL_ARRAY );
			glNormalPointer( GL_FLOAT, 0, transNorms.constData() );
		}

		// Do VCs if legacy or if either bslsp or bsesp is set
		bool doVCs = ( !bssp || bssp->hasSF2(ShaderFlags::SLSF2_Vertex_Colors) );

		if ( transColors.count() && scene->hasOption(Scene::DoVertexColors) && doVCs ) {
			glEnableClientState( GL_COLOR_ARRAY );
			glColorPointer( 4, GL_FLOAT, 0, transColors.constData() );
		} else {
			if ( !hasVertexColors && (bslsp && bslsp->hasVertexColors) ) {
				// Correctly blacken the mesh if SLSF2_Vertex_Colors is still on
				//	yet "Has Vertex Colors" is not.
				glColor( Color3( 0.0f, 0.0f, 0.0f ) );
			} else {
				glColor( Color3( 1.0f, 1.0f, 1.0f ) );
			}
		}
	}

	// TODO: Hotspot.  See about optimizing this.
	if ( !Node::SELECTING )
		shader = scene->renderer->setupProgram( this, shader );

	if ( isDoubleSided ) {
		glDisable( GL_CULL_FACE );
	}

	if ( !isLOD ) {
		// render the triangles
		if ( sortedTriangles.count() )
			glDrawElements( GL_TRIANGLES, sortedTriangles.count() * 3, GL_UNSIGNED_SHORT, sortedTriangles.constData() );

	} else if ( sortedTriangles.count() ) {
		auto lod0 = nif->get<uint>( iBlock, "LOD0 Size" );
		auto lod1 = nif->get<uint>( iBlock, "LOD1 Size" );
		auto lod2 = nif->get<uint>( iBlock, "LOD2 Size" );

		auto lod0tris = sortedTriangles.mid( 0, lod0 );
		auto lod1tris = sortedTriangles.mid( lod0, lod1 );
		auto lod2tris = sortedTriangles.mid( lod0 + lod1, lod2 );

		// If Level2, render all
		// If Level1, also render Level0
		switch ( scene->lodLevel ) {
		case Scene::Level0:
			if ( lod2tris.count() )
				glDrawElements( GL_TRIANGLES, lod2tris.count() * 3, GL_UNSIGNED_SHORT, lod2tris.constData() );
		case Scene::Level1:
			if ( lod1tris.count() )
				glDrawElements( GL_TRIANGLES, lod1tris.count() * 3, GL_UNSIGNED_SHORT, lod1tris.constData() );
		case Scene::Level2:
		default:
			if ( lod0tris.count() )
				glDrawElements( GL_TRIANGLES, lod0tris.count() * 3, GL_UNSIGNED_SHORT, lod0tris.constData() );
			break;
		}
	}

	// render the tristrips
	for ( auto & s : tristrips )
		glDrawElements( GL_TRIANGLE_STRIP, s.count(), GL_UNSIGNED_SHORT, s.constData() );

	if ( isDoubleSided ) {
		glEnable( GL_CULL_FACE );
	}

	if ( !Node::SELECTING )
		scene->renderer->stopProgram();

	glDisableClientState( GL_VERTEX_ARRAY );
	glDisableClientState( GL_NORMAL_ARRAY );
	glDisableClientState( GL_COLOR_ARRAY );

	glDisable( GL_POLYGON_OFFSET_FILL );

	glPointSize( 8.5 );
	if ( scene->isSelModeVertex() ) {
		drawVerts();
	}

	if ( transformRigid )
		glPopMatrix();
}

void Mesh::drawVerts() const
{
	glDisable( GL_LIGHTING );
	glNormalColor();

	glBegin( GL_POINTS );

	for ( int i = 0; i < transVerts.count(); i++ ) {
		if ( Node::SELECTING ) {
			int id = ID2COLORKEY( (shapeNumber << 16) + i );
			glColor4ubv( (GLubyte *)&id );
		}
		glVertex( transVerts.value( i ) );
	}

	// Highlight selected vertex
	if ( !Node::SELECTING && iData == scene->currentBlock ) {
		auto idx = scene->currentIndex;
		if ( idx.data( Qt::DisplayRole ).toString() == "Vertices" ) {
			glHighlightColor();
			glVertex( transVerts.value( idx.row() ) );
		}
	}

	glEnd();
}

void Mesh::drawSelection() const
{
	if ( scene->hasOption(Scene::ShowNodes) )
		Node::drawSelection();

	if ( isHidden() || !scene->isSelModeObject() )
		return;

	auto idx = scene->currentIndex;
	auto blk = scene->currentBlock;

	auto nif = NifModel::fromIndex( idx );
	if ( !nif )
		return;

	if ( blk != iBlock && blk != iData && blk != iSkinPart && blk != iSkinData
	     && ( !iTangentData.isValid() || blk != iTangentData ) )
	{
		return;
	}

	if ( transformRigid ) {
		glPushMatrix();
		glMultMatrix( viewTrans() );
	}

	glDisable( GL_LIGHTING );
	glDisable( GL_COLOR_MATERIAL );
	glDisable( GL_TEXTURE_2D );
	glDisable( GL_NORMALIZE );
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_FALSE );
	glEnable( GL_BLEND );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	glDisable( GL_ALPHA_TEST );

	glDisable( GL_CULL_FACE );

	glEnable( GL_POLYGON_OFFSET_FILL );
	glPolygonOffset( -1.0f, -2.0f );

	glLineWidth( 1.0 );
	glPointSize( 3.5 );

	QString n;
	int i = -1;

	if ( blk == iBlock || idx == iData ) {
		n = "Faces";
	} else if ( blk == iData || blk == iSkinPart ) {
		n = idx.data( NifSkopeDisplayRole ).toString();

		QModelIndex iParent = idx.parent();
		if ( iParent.isValid() && iParent != iData ) {
			n = iParent.data( NifSkopeDisplayRole ).toString();
			i = idx.row();
		}
	} else if ( blk == iTangentData ) {
		n = "TSpace";
	} else {
		n = idx.data( NifSkopeDisplayRole ).toString();
	}

	glDepthFunc( GL_LEQUAL );
	glNormalColor();

	glPolygonMode( GL_FRONT_AND_BACK, GL_POINT );

	if ( n == "Vertices" || n == "Normals" || n == "Vertex Colors"
	     || n == "UV Sets" || n == "Tangents" || n == "Bitangents" )
	{
		glBegin( GL_POINTS );

		for ( int j = 0; j < transVerts.count(); j++ )
			glVertex( transVerts.value( j ) );

		glEnd();

		if ( i >= 0 ) {
			glDepthFunc( GL_ALWAYS );
			glHighlightColor();
			glBegin( GL_POINTS );
			glVertex( transVerts.value( i ) );
			glEnd();
		}
	}

	if ( n == "Points" ) {
		glBegin( GL_POINTS );
		auto nif = NifModel::fromIndex( iData );
		QModelIndex points = nif->getIndex( iData, "Points" );

		if ( points.isValid() ) {
			for ( int j = 0; j < nif->rowCount( points ); j++ ) {
				QModelIndex iPoints = points.child( j, 0 );

				for ( int k = 0; k < nif->rowCount( iPoints ); k++ ) {
					glVertex( transVerts.value( nif->get<quint16>( iPoints.child( k, 0 ) ) ) );
				}
			}
		}

		glEnd();

		if ( i >= 0 ) {
			glDepthFunc( GL_ALWAYS );
			glHighlightColor();
			glBegin( GL_POINTS );
			QModelIndex iPoints = points.child( i, 0 );

			if ( nif->isArray( idx ) ) {
				for ( int j = 0; j < nif->rowCount( iPoints ); j++ ) {
					glVertex( transVerts.value( nif->get<quint16>( iPoints.child( j, 0 ) ) ) );
				}
			} else {
				iPoints = idx.parent();
				glVertex( transVerts.value( nif->get<quint16>( iPoints.child( i, 0 ) ) ) );
			}

			glEnd();
		}
	}

	glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );

	// TODO: Reenable as an alternative to MSAA when MSAA is not supported
	//glEnable( GL_LINE_SMOOTH );
	//glHint( GL_LINE_SMOOTH_HINT, GL_NICEST );

	if ( n == "Normals" || n == "TSpace" ) {
		float normalScale = bounds().radius / 20;

		if ( normalScale < 0.1f )
			normalScale = 0.1f;

		glBegin( GL_LINES );

		for ( int j = 0; j < transVerts.count() && j < transNorms.count(); j++ ) {
			glVertex( transVerts.value( j ) );
			glVertex( transVerts.value( j ) + transNorms.value( j ) * normalScale );
		}

		if ( n == "TSpace" ) {
			for ( int j = 0; j < transVerts.count() && j < transTangents.count() && j < transBitangents.count(); j++ ) {
				glVertex( transVerts.value( j ) );
				glVertex( transVerts.value( j ) + transTangents.value( j ) * normalScale );
				glVertex( transVerts.value( j ) );
				glVertex( transVerts.value( j ) + transBitangents.value( j ) * normalScale );
			}
		}

		glEnd();

		if ( i >= 0 ) {
			glDepthFunc( GL_ALWAYS );
			glHighlightColor();
			glBegin( GL_LINES );
			glVertex( transVerts.value( i ) );
			glVertex( transVerts.value( i ) + transNorms.value( i ) * normalScale );
			glEnd();
		}
	}

	if ( n == "Tangents" ) {
		float normalScale = bounds().radius / 20;
		normalScale /= 2.0f;

		if ( normalScale < 0.1f )
			normalScale = 0.1f;

		glBegin( GL_LINES );

		for ( int j = 0; j < transVerts.count() && j < transTangents.count(); j++ ) {
			glVertex( transVerts.value( j ) );
			glVertex( transVerts.value( j ) + transTangents.value( j ) * normalScale * 2 );
			glVertex( transVerts.value( j ) );
			glVertex( transVerts.value( j ) - transTangents.value( j ) * normalScale / 2 );
		}

		glEnd();

		if ( i >= 0 ) {
			glDepthFunc( GL_ALWAYS );
			glHighlightColor();
			glBegin( GL_LINES );
			glVertex( transVerts.value( i ) );
			glVertex( transVerts.value( i ) + transTangents.value( i ) * normalScale * 2 );
			glVertex( transVerts.value( i ) );
			glVertex( transVerts.value( i ) - transTangents.value( i ) * normalScale / 2 );
			glEnd();
		}
	}

	if ( n == "Bitangents" ) {
		float normalScale = bounds().radius / 20;
		normalScale /= 2.0f;

		if ( normalScale < 0.1f )
			normalScale = 0.1f;

		glBegin( GL_LINES );

		for ( int j = 0; j < transVerts.count() && j < transBitangents.count(); j++ ) {
			glVertex( transVerts.value( j ) );
			glVertex( transVerts.value( j ) + transBitangents.value( j ) * normalScale * 2 );
			glVertex( transVerts.value( j ) );
			glVertex( transVerts.value( j ) - transBitangents.value( j ) * normalScale / 2 );
		}

		glEnd();

		if ( i >= 0 ) {
			glDepthFunc( GL_ALWAYS );
			glHighlightColor();
			glBegin( GL_LINES );
			glVertex( transVerts.value( i ) );
			glVertex( transVerts.value( i ) + transBitangents.value( i ) * normalScale * 2 );
			glVertex( transVerts.value( i ) );
			glVertex( transVerts.value( i ) - transBitangents.value( i ) * normalScale / 2 );
			glEnd();
		}
	}

	if ( n == "Faces" || n == "Triangles" ) {
		glLineWidth( 1.5f );

		for ( const Triangle& tri : triangles ) {
			glBegin( GL_TRIANGLES );
			glVertex( transVerts.value( tri.v1() ) );
			glVertex( transVerts.value( tri.v2() ) );
			glVertex( transVerts.value( tri.v3() ) );
			//glVertex( transVerts.value( tri.v1() ) );
			glEnd();
		}

		if ( i >= 0 ) {
			glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
			glDepthFunc( GL_ALWAYS );
			glHighlightColor();
			Triangle tri = triangles.value( i );
			glBegin( GL_TRIANGLES );
			glVertex( transVerts.value( tri.v1() ) );
			glVertex( transVerts.value( tri.v2() ) );
			glVertex( transVerts.value( tri.v3() ) );
			//glVertex( transVerts.value( tri.v1() ) );
			glEnd();
			glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
		}
	}

	if ( n == "Faces" || n == "Strips" || n == "Strip Lengths" ) {
		glLineWidth( 1.5f );

		for ( const TriStrip& strip : tristrips ) {
			quint16 a = strip.value( 0 );
			quint16 b = strip.value( 1 );

			for ( int v = 2; v < strip.count(); v++ ) {
				quint16 c = strip[v];

				if ( a != b && b != c && c != a ) {
					glBegin( GL_LINE_STRIP );
					glVertex( transVerts.value( a ) );
					glVertex( transVerts.value( b ) );
					glVertex( transVerts.value( c ) );
					glVertex( transVerts.value( a ) );
					glEnd();
				}

				a = b;
				b = c;
			}
		}

		if ( i >= 0 && !tristrips.isEmpty() ) {
			TriStrip strip = tristrips[i];

			quint16 a = strip.value( 0 );
			quint16 b = strip.value( 1 );

			for ( int v = 2; v < strip.count(); v++ ) {
				quint16 c = strip[v];

				if ( a != b && b != c && c != a ) {
					glDepthFunc( GL_ALWAYS );
					glHighlightColor();
					glBegin( GL_LINE_STRIP );
					glVertex( transVerts.value( a ) );
					glVertex( transVerts.value( b ) );
					glVertex( transVerts.value( c ) );
					glVertex( transVerts.value( a ) );
					glEnd();
				}

				a = b;
				b = c;
			}
		}
	}

	if ( n == "Partitions" ) {

		for ( int c = 0; c < partitions.count(); c++ ) {
			if ( c == i )
				glHighlightColor();
			else
				glNormalColor();

			QVector<int> vmap = partitions[c].vertexMap;

			for ( const Triangle& tri : partitions[c].triangles ) {
				glBegin( GL_LINE_STRIP );
				glVertex( transVerts.value( vmap.value( tri.v1() ) ) );
				glVertex( transVerts.value( vmap.value( tri.v2() ) ) );
				glVertex( transVerts.value( vmap.value( tri.v3() ) ) );
				glVertex( transVerts.value( vmap.value( tri.v1() ) ) );
				glEnd();
			}
			for ( const TriStrip& strip : partitions[c].tristrips ) {
				quint16 a = vmap.value( strip.value( 0 ) );
				quint16 b = vmap.value( strip.value( 1 ) );

				for ( int v = 2; v < strip.count(); v++ ) {
					quint16 c = vmap.value( strip[v] );

					if ( a != b && b != c && c != a ) {
						glBegin( GL_LINE_STRIP );
						glVertex( transVerts.value( a ) );
						glVertex( transVerts.value( b ) );
						glVertex( transVerts.value( c ) );
						glVertex( transVerts.value( a ) );
						glEnd();
					}

					a = b;
					b = c;
				}
			}
		}
	}

	if ( n == "Bone List" ) {
		if ( nif->isArray( idx ) ) {
			for ( int i = 0; i < nif->rowCount( idx ); i++ )
				boneSphere( nif, idx.child( i, 0 ) );
		} else {
			boneSphere( nif, idx );
		}
	}

	glDisable( GL_POLYGON_OFFSET_FILL );

	if ( transformRigid )
		glPopMatrix();
}

QString Mesh::textStats() const
{
	return Node::textStats() + QString( "\nshader: %1\n" ).arg( shader );
}
