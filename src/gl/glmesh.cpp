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

#include "gl/glscene.h"
#include "io/nifstream.h"
#include "lib/nvtristripwrapper.h"

#include <QBuffer>


//! @file glmesh.cpp Scene management for visible meshes such as NiTriShapes.

void Mesh::updateImpl( const NifModel * nif, const QModelIndex & index )
{
	Shape::updateImpl(nif, index);

	if ( index == iData || index == iExtraData )
		needUpdateData = true;
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

	Q_ASSERT( verts.size() == maxIndex + 1 );
	Q_ASSERT( indices.size() == totalIndices );
	numVerts = verts.count();

	// Make geometry
	int nTotalTris = indices.size() / 3;
	triangles.resize( nTotalTris );
	auto typeField = block["Primitive Type"];
	auto meshPrimitiveType = typeField.value<uint>();
	switch ( meshPrimitiveType ) {
	case NiMesh::PRIMITIVE_TRIANGLES:
		for ( int t = 0, k = 0; t < nTotalTris; t++, k += 3 )
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
	NifFieldConst block = nif->block(iBlock);

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
				block.reportError( tr( "Block has multiple data blocks" ) );
		} else if ( childBlock.inherits( "NiSkinInstance" ) ) {
			if ( !skinBlock )
				skinBlock = childBlock;
			else if ( skinBlock != childBlock )
				block.reportError( tr( "Block has multiple skin instances" ) );
		}
	}
	if ( !dataBlock )
		return;
	iData = dataBlock.toIndex(); // ???

	NifFieldConst skinDataBlock, skinPartBlock;
	if ( skinBlock ) {
		isSkinned = true;
		iSkin = skinBlock.toIndex(); // ???

		skinDataBlock = skinBlock.child("Data").linkBlock("NiSkinData");
		iSkinData = skinDataBlock.toIndex(); // ???

		skinPartBlock = skinBlock.child("Skin Partition").linkBlock("NiSkinPartition");
		if ( !skinPartBlock && skinDataBlock ) {
			// nif versions < 10.2.0.0 have skin partition linked in the skin data block
			skinPartBlock = skinDataBlock.child("Skin Partition").linkBlock("NiSkinPartition");
		}
		iSkinPart = skinPartBlock.toIndex(); // ???
	}

	// Fill vertex data
	mainVertexRoot = dataBlock.child("Vertices");
	verts = mainVertexRoot.array<Vector3>();
	numVerts = verts.count();
	addVertexSelection( mainVertexRoot, VertexSelectionType::VERTICES );

	auto normalsField = dataBlock.child("Normals");
	if ( normalsField ) {
		reportCountMismatch( normalsField, mainVertexRoot, dataBlock );
		hasVertexNormals = true;
		norms = normalsField.array<Vector3>();
		addVertexSelection( normalsField, VertexSelectionType::NORMALS );
	}

	NifFieldConst extraTangents;
	for ( auto extraLink : block.child("Extra Data List").linkArray() ) {
		auto extraBlock = nif->block( extraLink );
		if ( extraBlock.inherits("NiBinaryExtraData") && extraBlock.child("Name").value<QString>() == QStringLiteral("Tangent space (binormal & tangent vectors)") ) {
			extraTangents = extraBlock;
			break;
		}
	}
	if ( extraTangents ) {
		hasVertexTangents = true;
		hasVertexBitangents = true;
		iExtraData = extraTangents.toIndex(); // ???
		auto extraDataRoot = extraTangents["Binary Data"];
		QByteArray extraData = extraDataRoot.value<QByteArray>();
		int nExtraCount = extraData.count() / ( sizeof(Vector3) * 2 );
		reportCountMismatch(extraDataRoot, nExtraCount, mainVertexRoot, numVerts, block );
		tangents.resize(nExtraCount);
		bitangents.resize(nExtraCount);
		Vector3 * t = (Vector3 *) extraData.data();
		for ( int i = 0; i < nExtraCount; i++ )
			tangents[i] = *t++;
		for ( int i = 0; i < nExtraCount; i++ )
			bitangents[i] = *t++;
		addVertexSelection( extraDataRoot, VertexSelectionType::EXTRA_TANGENTS );
	} else {
		auto tangentsField = dataBlock.child("Tangents");
		if ( tangentsField ) {
			reportCountMismatch( tangentsField, mainVertexRoot, dataBlock );
			hasVertexTangents = true;
			tangents = tangentsField.array<Vector3>();
			addVertexSelection( tangentsField, VertexSelectionType::TANGENTS );
		}

		auto bitangentsField = dataBlock.child("Bitangents");
		if ( bitangentsField ) {
			reportCountMismatch( tangentsField, mainVertexRoot, dataBlock );
			hasVertexBitangents = true;
			bitangents = bitangentsField.array<Vector3>();
			addVertexSelection( bitangentsField, VertexSelectionType::BITANGENTS );
		}
	}

	auto uvSetsRoot = dataBlock.child("UV Sets");
	if ( uvSetsRoot ) {
		hasVertexUVs = true;
		for ( auto uvSetField : uvSetsRoot.iter() ) {
			reportCountMismatch( uvSetField, mainVertexRoot, dataBlock );
			coords.append( uvSetField.array<Vector2>() );
			addVertexSelection( uvSetField, VertexSelectionType::VERTICES );
		}
		addVertexSelection( uvSetsRoot, VertexSelectionType::VERTEX_ROOT );
	}

	auto colorsField = dataBlock.child("Vertex Colors");
	if ( colorsField ) {
		reportCountMismatch( colorsField, mainVertexRoot, dataBlock );
		hasVertexColors = true;
		colors = colorsField.array<Color4>();
		addVertexSelection( colorsField, VertexSelectionType::VERTICES );
	}

	// Fill triangle/strips data
	if ( !skinPartBlock ) {
		if ( dataBlock.isBlockType("NiTriShapeData") ) {
			addTriangles( dataBlock.child("Triangles") );
		} else if ( dataBlock.isBlockType("NiTriStripsData") ) {
			addStrips( dataBlock.child("Points"), 0 );
		} else {
			dataBlock.reportError( tr("Could not find triangles or strips in data block of type '%1'.").arg( dataBlock.name() ) );
		}
	}

	// Fill skinning and skeleton data
	if ( skinBlock ) {
		skeletonRoot = skinBlock.child("Skeleton Root").link();
		
		skeletonTrans = Transform( skinDataBlock );

		// Fill bones
		auto nodeListRoot = skinDataBlock.child("Bone List");
		initSkinBones( skinBlock.child("Bones"), nodeListRoot, block );
		int nTotalBones = bones.count();

		// Fill vertex weights, triangles, strips
		if ( skinPartBlock ) {
			auto partRoot = skinPartBlock.child("Partitions");
			int nPartitions = partRoot.childCount();

			QVector<TriangleRange *> blockTriRanges( nPartitions );
			QVector<StripRange *> blockStripRanges( nPartitions );

			QVector<bool> weightedVertices( numVerts );

			for ( int iPart = 0; iPart < nPartitions; iPart++ ) {
				auto partEntry = partRoot[iPart];

				// Vertex map
				auto vertexMapRoot = partEntry.child("Vertex Map");
				QVector<int> partVertexMap;
				int nPartMappedVertices = vertexMapRoot.childCount();
				if ( nPartMappedVertices > 0 ) {
					partVertexMap.reserve( nPartMappedVertices );
					for ( auto mapEntry : vertexMapRoot.iter() ) {
						int v = mapEntry.value<int>();
						if ( v < 0 || v >= numVerts )
							mapEntry.reportError( tr("Invalid vertex index %1").arg(v) );
						partVertexMap << v;
					}
					addVertexSelection( vertexMapRoot, VertexSelectionType::VERTICES, vertexMapRoot );
				}

				// Bone map
				auto boneMapRoot = partEntry.child("Bones");
				int nPartBones = boneMapRoot.childCount();
				QVector<int> partBoneMap;
				partBoneMap.reserve( nPartBones );
				for ( auto mapEntry : boneMapRoot.iter() ) {
					int b = mapEntry.value<int>();
					if ( b < 0 || b >= nTotalBones )
						mapEntry.reportError( tr("Invalid bone index %1").arg(b) );
					partBoneMap << b;
				}

				// Vertex weights
				int weightsPerVertex = partEntry.child("Num Weights Per Vertex").value<int>();
				auto boneIndicesRoot = partEntry.child("Bone Indices");
				auto weightsRoot = partEntry.child("Vertex Weights");
				reportCountMismatch( boneIndicesRoot, weightsRoot, partEntry );
				int nDataVerts = std::min( boneIndicesRoot.childCount(), weightsRoot.childCount() );
				if ( nPartMappedVertices > 0 ) {
					reportCountMismatch( boneIndicesRoot, vertexMapRoot, partEntry );
					if ( nPartMappedVertices < nDataVerts )
						nDataVerts = nPartMappedVertices;
					addVertexSelection( boneIndicesRoot, VertexSelectionType::VERTICES, vertexMapRoot );
					addVertexSelection( weightsRoot, VertexSelectionType::VERTICES, vertexMapRoot );
				} else {
					if ( nDataVerts > numVerts )
						nDataVerts = numVerts;
					addVertexSelection( boneIndicesRoot, VertexSelectionType::VERTICES );
					addVertexSelection( weightsRoot, VertexSelectionType::VERTICES );
				}
				for ( int v = 0; v < nDataVerts; v++ ) {
					int vind;
					if ( nPartMappedVertices > 0 ) {
						vind = partVertexMap[v];
						if ( vind < 0 || vind >= numVerts )
							continue;
					} else {
						vind = v;
					}

					if ( weightedVertices[vind] )
						continue;
					weightedVertices[vind] = true;

					auto bentry = boneIndicesRoot[v];
					auto wentry = weightsRoot[v];
					for ( int wind = 0; wind < weightsPerVertex; wind++ ) {
						float w = wentry[wind].value<float>();
						if ( w == 0.0f )
							continue;
						int b = bentry[wind].value<int>();
						if ( b < 0 || b >= nPartBones ) {
							bentry[wind].reportError( tr("Invalid bone index %1").arg(b) );
							continue;
						}
						int bind = partBoneMap[b];
						if ( bind < 0 || bind >= nTotalBones )
							continue;
						bones[bind].vertexWeights << VertexWeight( vind, w );
					}
				}

				// Triangles
				auto partTrisRoot = partEntry.child("Triangles");
				if ( partTrisRoot ) {
					int iPartStart = triangles.count();

					if ( nPartMappedVertices > 0 ) {
						QVector<Triangle> tris;
						tris.reserve( partTrisRoot.childCount() );
						for ( auto triEntry : partTrisRoot.iter() ) {
							Triangle t = triEntry.value<Triangle>();
							bool success = true;
							for ( TriVertexIndex & tv : t.v ) {
								if ( tv < nPartMappedVertices ) {
									tv = partVertexMap[tv];
								} else {
									triEntry.reportError( tr("Invalid vertex map index %1").arg(tv) );
									success = false;
								}
							}
							if ( !success ) {
								// Intentionally break all vertices of the triangle if any of them failed mapping.
								// Then it will be discarded on post-update triangles cleanup.
								// Even in the worst case (the total number of vertices in the shape is (Triangle::MAX_VERTEX_INDEX + 1) or greater) 
								// the triangle still won't be rendered.
								t.set( Triangle::MAX_VERTEX_INDEX, Triangle::MAX_VERTEX_INDEX, Triangle::MAX_VERTEX_INDEX );
							}
							tris << t;
						}
						addTriangles( partTrisRoot, tris );
					} else {
						addTriangles( partTrisRoot);
					}

					blockTriRanges[iPart] = addTriangleRange( partEntry, TriangleRange::FLAG_HIGHLIGHT, iPartStart );
				}

				// Strips
				auto partStripsRoot = partEntry.child("Strips");
				if ( partStripsRoot ) {
					int iPartStart = stripTriangles.count();

					if ( nPartMappedVertices > 0 ) {
						for ( auto stripEntry: partStripsRoot.iter() ) {
							TriStrip stripPoints;
							stripPoints.reserve( stripEntry.childCount() );
							for ( auto pointEntry : stripEntry.iter() ) {
								TriVertexIndex p = pointEntry.value<TriVertexIndex>();
								if ( p >= nPartMappedVertices ) {
									pointEntry.reportError( tr("Invalid vertex map index %1").arg(p) );
								}
								stripPoints << p;
							}

							QVector<Triangle> stripTris = triangulateStrip( stripPoints );
							for ( auto & t : stripTris ) {
								if ( t[0] >= nPartMappedVertices || t[1] >= nPartMappedVertices || t[2] >= nPartMappedVertices ) {
									// Intentionally break all vertices of the triangle if any of them failed mapping.
									// Then it will be discarded on post-update triangles cleanup.
									// Even in the worst case (the total number of vertices in the shape is (Triangle::MAX_VERTEX_INDEX + 1) or greater) 
									// the triangle still won't be rendered.
									t.set( Triangle::MAX_VERTEX_INDEX, Triangle::MAX_VERTEX_INDEX, Triangle::MAX_VERTEX_INDEX );
								}
							}
							addStrip( stripEntry, stripTris, vertexMapRoot );
						}
						addStripRange( partStripsRoot, TriangleRange::FLAG_HIGHLIGHT, iPartStart );
					} else {
						addStrips( partStripsRoot, TriangleRange::FLAG_HIGHLIGHT );
					}

					blockStripRanges[iPart] = addStripRange( partEntry, TriangleRange::FLAG_HIGHLIGHT, iPartStart );
				}

				// Add an empty selection range for partEntry if the partition has no triangles and strips at all.
				TriangleRange * r = blockTriRanges[iPart];
				if ( !r ) {
					r = blockStripRanges[iPart];
					if ( !r )
						r = addTriangleRange( partEntry, TriangleRange::FLAG_HIGHLIGHT, 0, 0 );
				}
				addPartitionBoneSelection( boneMapRoot, r );
			}

			// Add selection ranges for Partitions in skinBlock
			auto otherPartRoot = skinBlock.child("Partitions");
			for ( int iPart = 0, nOtherPartititions = otherPartRoot.childCount(); iPart < nOtherPartititions; iPart++ ) {
				const NifSkopeFlagsType PART_RANGE_FLAGS = TriangleRange::FLAG_HIGHLIGHT | TriangleRange::FLAG_DEEP;

				auto rangeTris = blockTriRanges.value( iPart, nullptr );
				if ( rangeTris )
					addTriangleRange( otherPartRoot[iPart], PART_RANGE_FLAGS, rangeTris->start, rangeTris->length );

				auto rangeStrips = blockStripRanges.value( iPart, nullptr );
				if ( rangeStrips )
					addStripRange( otherPartRoot[iPart], PART_RANGE_FLAGS, rangeStrips->start, rangeStrips->length, NifFieldConst() );

				// Fallback
				if ( !rangeTris && !rangeStrips )
					addTriangleRange( otherPartRoot[iPart], PART_RANGE_FLAGS, 0, 0 );
			}

		} else if ( skinDataBlock.child("Has Vertex Weights").value<unsigned char>() ) {
			for ( int bind = 0, nListedBones = nodeListRoot.childCount(); bind < nListedBones; bind++ ) {
				auto inData = nodeListRoot[bind].child("Vertex Weights");
				int nWeights = inData.childCount();
				if ( nWeights <= 0 )
					continue;

				auto firstWeight = inData[0];
				int iIndexField = firstWeight["Index"].row();
				int iWeightField = firstWeight["Weight"].row();
				if ( iIndexField < 0 || iWeightField < 0 )
					continue;

				auto & outWeights = bones[bind].vertexWeights;
				outWeights.reserve( nWeights );
				for ( auto wentry : inData.iter() ) {
					float w = wentry[iWeightField].value<float>();
					if ( w == 0.0f )
						continue;
					int vind = wentry[iIndexField].value<int>();
					if ( vind < 0 || vind >= numVerts ) {
						wentry[iIndexField].reportError( tr("Invalid vertex index %1").arg(vind) );
						continue;
					}
					outWeights << VertexWeight( vind, w );
				}
			}
		}
	}

	// LODs
	if ( block.isBlockType( "BSLODTriShape" ) ) {
		initLodData( block );
	}

	// Bounding sphere
	addBoundSphereSelection( dataBlock.child("Bounding Sphere") );
}

void Mesh::transformShapes()
{
	if ( isHidden() )
		return;

	Node::transformShapes();

	if ( doSkinning() ) {
		// TODO (Gavrant): I've no idea why it requires different transforms depending on whether it's partitioned or not.
		Transform baseTrans = iSkinPart.isValid() ? scene->view : ( viewTrans() * skeletonTrans );
		applySkinningTransforms( baseTrans );
	} else {
		applyRigidTransforms();
	}

	// Colors
	MaterialProperty * matprop = findProperty<MaterialProperty>();
	applyColorTransforms( matprop ? matprop->alphaValue() : 1.0f );
}

BoundSphere Mesh::bounds() const
{
	if ( needUpdateBounds ) {
		needUpdateBounds = false;
		boundSphere = BoundSphere( verts );
	}

	return worldTrans() * boundSphere;
}

QString Mesh::textStats() const
{
	return Node::textStats() + QString( "\nshader: %1\n" ).arg( shader );
}
