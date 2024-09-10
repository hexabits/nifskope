#include "nvtristripwrapper.h"

#include <NvTriStrip.h>

#include "model/nifmodel.h"

QVector<TriStrip> stripifyTriangles( const QVector<Triangle> & triangles, bool stitch )
{
	QVector<TriStrip> strips;

	int nTris = triangles.count();
	if ( nTris > 0 ) {
		PrimitiveGroup * groups  = nullptr;
		unsigned short numGroups = 0;
		SetStitchStrips( stitch );
		//SetCacheSize( 64 );
		auto pTriPoints = reinterpret_cast<const TriVertexIndex *>( triangles.constData() );
		GenerateStrips( pTriPoints, nTris * 3, &groups, &numGroups );

		if ( groups ) {
			auto pGroup = groups;
			for ( int g = 0; g < numGroups; g++, pGroup++ ) {
				if ( pGroup->type == PT_STRIP ) {
					TriStrip strip;

					int nStripPoints = pGroup->numIndices;
					strip.reserve( nStripPoints );
					auto pOut = strip.data();
					auto pIn = pGroup->indices;
					for ( ; nStripPoints > 0; nStripPoints--, pOut++, pIn++ ) {
						*pOut = *pIn;
					}

					strips.append( strip );
				}
			}

			delete [] groups;
		}

	}

	return strips;
}

QVector<Triangle> triangulateStrip( const TriStrip & stripPoints )
{
	QVector<Triangle> tris;

	int nStripTris = stripPoints.count() - 2;
	if ( nStripTris > 0 ) {
		tris.reserve( nStripTris );

		auto pPoints = stripPoints.constData();
		for ( int i = 0; i < nStripTris; i++, pPoints++ ) {
			auto a = pPoints[0];
			auto b = pPoints[1];
			auto c = pPoints[2];

			if ( a != b && b != c && c != a ) {
				if ( (i & 1) == 0 )
					tris.append( Triangle( a, b, c ) );
				else
					tris.append( Triangle( a, c, b ) );
			}
		}
	}

	return tris;
}

QVector<Triangle> triangulateStrips( const NifModel * nif, const QModelIndex & iStrips )
{
	QVector<Triangle> tris;
	for ( int r = 0; r < nif->rowCount( iStrips ); r++ ) {
		tris += triangulateStrip( nif->getArray<TriVertexIndex>( iStrips.child( r, 0 ) ) );
	}
	return tris;
}
