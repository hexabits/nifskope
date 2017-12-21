#include "moppcode.h"

bool HavokMoppCode::Initialize()
{
	if ( !hMoppLib ) {
		SetDllDirectoryA( QCoreApplication::applicationDirPath().toLocal8Bit().constData() );
		hMoppLib = LoadLibraryA( "NifMopp.dll" );
		GenerateMoppCode   = (fnGenerateMoppCode)GetProcAddress( hMoppLib, "GenerateMoppCode" );
		RetrieveMoppCode   = (fnRetrieveMoppCode)GetProcAddress( hMoppLib, "RetrieveMoppCode" );
		RetrieveMoppScale  = (fnRetrieveMoppScale)GetProcAddress( hMoppLib, "RetrieveMoppScale" );
		RetrieveMoppOrigin = (fnRetrieveMoppOrigin)GetProcAddress( hMoppLib, "RetrieveMoppOrigin" );
		GenerateMoppCodeWithSubshapes = (fnGenerateMoppCodeWithSubshapes)GetProcAddress( hMoppLib, "GenerateMoppCodeWithSubshapes" );
	}

	return (GenerateMoppCode && RetrieveMoppCode && RetrieveMoppScale && RetrieveMoppOrigin);
}

QByteArray HavokMoppCode::CalculateMoppCode( QVector<Vector3> const & verts, QVector<Triangle> const & tris, Vector3 * origin, float * scale )
{
	QByteArray code;

	if ( Initialize() ) {
		int len = GenerateMoppCode( verts.size(), &verts[0], tris.size(), &tris[0] );

		if ( len > 0 ) {
			code.resize( len );

			if ( 0 != RetrieveMoppCode( len, code.data() ) ) {
				if ( scale )
				RetrieveMoppScale( scale );

				if ( origin )
				RetrieveMoppOrigin( origin );
			} else {
				code.clear();
			}
		}
	}

	return code;
}

QByteArray HavokMoppCode::CalculateMoppCode( QVector<int> const & subShapesVerts,
	QVector<Vector3> const & verts,
	QVector<Triangle> const & tris,
	Vector3 * origin, float * scale )
	{
		QByteArray code;

		if ( Initialize() ) {
			int len;

			if ( GenerateMoppCodeWithSubshapes )
			len = GenerateMoppCodeWithSubshapes( subShapesVerts.size(), &subShapesVerts[0], verts.size(), &verts[0], tris.size(), &tris[0] );
			else
			len = GenerateMoppCode( verts.size(), &verts[0], tris.size(), &tris[0] );

			if ( len > 0 ) {
				code.resize( len );

				if ( 0 != RetrieveMoppCode( len, code.data() ) ) {
					if ( scale )
					RetrieveMoppScale( scale );

					if ( origin )
					RetrieveMoppOrigin( origin );
				} else {
					code.clear();
				}
			}
		}

		return code;
	}



	bool spMoppCode::isApplicable( const NifModel * nif, const QModelIndex & index )
	{
		if ( nif->getUserVersion() != 10 && nif->getUserVersion() != 11 )
		return false;

		if ( TheHavokCode.Initialize() ) {
			//QModelIndex iData = nif->getBlock( nif->getLink( index, "Data" ) );

			if ( nif->isNiBlock( index, "bhkMoppBvTreeShape" ) ) {
				return ( nif->checkVersion( 0x14000004, 0x14000005 )
				|| nif->checkVersion( 0x14020007, 0x14020007 ) );
			}
		}

		return false;
	}

	QModelIndex spMoppCode::cast( NifModel * nif, const QModelIndex & iBlock )
	{
		if ( !TheHavokCode.Initialize() ) {
			Message::critical( nullptr, Spell::tr( "Unable to locate NifMopp.dll" ) );
			return iBlock;
		}

		QPersistentModelIndex ibhkMoppBvTreeShape = iBlock;

		QModelIndex ibhkPackedNiTriStripsShape = nif->getBlock( nif->getLink( ibhkMoppBvTreeShape, "Shape" ) );

		if ( !nif->isNiBlock( ibhkPackedNiTriStripsShape, "bhkPackedNiTriStripsShape" ) ) {
			Message::warning( nullptr, Spell::tr( "Only bhkPackedNiTriStripsShape is supported at this time." ) );
			return iBlock;
		}

		QModelIndex ihkPackedNiTriStripsData = nif->getBlock( nif->getLink( ibhkPackedNiTriStripsShape, "Data" ) );

		if ( !nif->isNiBlock( ihkPackedNiTriStripsData, "hkPackedNiTriStripsData" ) )
		return iBlock;

		QVector<int> subshapeVerts;

		if ( nif->checkVersion( 0x14000004, 0x14000005 ) ) {
			int nSubShapes = nif->get<int>( ibhkPackedNiTriStripsShape, "Num Sub Shapes" );
			QModelIndex ihkSubShapes = nif->getIndex( ibhkPackedNiTriStripsShape, "Sub Shapes" );
			subshapeVerts.resize( nSubShapes );

			for ( int t = 0; t < nSubShapes; t++ ) {
				subshapeVerts[t] = nif->get<int>( ihkSubShapes.child( t, 0 ), "Num Vertices" );
			}
		} else if ( nif->checkVersion( 0x14020007, 0x14020007 ) ) {
			int nSubShapes = nif->get<int>( ihkPackedNiTriStripsData, "Num Sub Shapes" );
			QModelIndex ihkSubShapes = nif->getIndex( ihkPackedNiTriStripsData, "Sub Shapes" );
			subshapeVerts.resize( nSubShapes );

			for ( int t = 0; t < nSubShapes; t++ ) {
				subshapeVerts[t] = nif->get<int>( ihkSubShapes.child( t, 0 ), "Num Vertices" );
			}
		}

		QVector<Vector3> verts = nif->getArray<Vector3>( ihkPackedNiTriStripsData, "Vertices" );
		QVector<Triangle> triangles;

		int nTriangles = nif->get<int>( ihkPackedNiTriStripsData, "Num Triangles" );
		QModelIndex iTriangles = nif->getIndex( ihkPackedNiTriStripsData, "Triangles" );
		triangles.resize( nTriangles );

		for ( int t = 0; t < nTriangles; t++ ) {
			triangles[t] = nif->get<Triangle>( iTriangles.child( t, 0 ), "Triangle" );
		}

		if ( verts.isEmpty() || triangles.isEmpty() ) {
			Message::critical( nullptr, Spell::tr( "Insufficient data to calculate MOPP code" ),
			Spell::tr("Vertices: %1, Triangles: %2").arg( !verts.isEmpty() ).arg( !triangles.isEmpty() )
		);
		return iBlock;
	}

	Vector3 origin;
	float scale;
	QByteArray moppcode = TheHavokCode.CalculateMoppCode( subshapeVerts, verts, triangles, &origin, &scale );

	if ( moppcode.size() == 0 ) {
		Message::critical( nullptr, Spell::tr( "Failed to generate MOPP code" ) );
	} else {
		QModelIndex iCodeOrigin = nif->getIndex( ibhkMoppBvTreeShape, "Origin" );
		nif->set<Vector3>( iCodeOrigin, origin );

		QModelIndex iCodeScale = nif->getIndex( ibhkMoppBvTreeShape, "Scale" );
		nif->set<float>( iCodeScale, scale );

		QModelIndex iCodeSize = nif->getIndex( ibhkMoppBvTreeShape, "MOPP Data Size" );
		QModelIndex iCode = nif->getIndex( ibhkMoppBvTreeShape, "MOPP Data" ).child( 0, 0 );

		if ( iCodeSize.isValid() && iCode.isValid() ) {
			nif->set<int>( iCodeSize, moppcode.size() );
			nif->updateArray( iCode );
			nif->set<QByteArray>( iCode, moppcode );
		}
	}

	return iBlock;
}

REGISTER_SPELL( spMoppCode )

bool spAllMoppCodes::isApplicable( const NifModel * nif, const QModelIndex & idx )
{
	if ( nif && nif->getUserVersion() != 10 && nif->getUserVersion() != 11 )
	return false;

	if ( TheHavokCode.Initialize() ) {
		if ( nif && !idx.isValid() ) {
			return ( nif->checkVersion( 0x14000004, 0x14000005 )
			|| nif->checkVersion( 0x14020007, 0x14020007 ) );
		}
	}

	return false;
}

QModelIndex spAllMoppCodes::cast( NifModel * nif, const QModelIndex & )
{
	QList<QPersistentModelIndex> indices;

	spMoppCode TSpacer;

	for ( int n = 0; n < nif->getBlockCount(); n++ ) {
		QModelIndex idx = nif->getBlock( n );

		if ( TSpacer.isApplicable( nif, idx ) )
		indices << idx;
	}

	for ( const QModelIndex& idx : indices ) {
		TSpacer.castIfApplicable( nif, idx );
	}

	return QModelIndex();
}


REGISTER_SPELL( spAllMoppCodes )
