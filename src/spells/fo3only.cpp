#include "fo3only.h"

bool spFO3FixShapeDataName::isApplicable( const NifModel * nif, const QModelIndex & index )
{
	//if ( !index.isValid() )
	//	return false;

	if ( !nif->checkVersion( 0x14020007, 0x14020007 ) || (nif->getUserVersion() != 11) )
	return false;

	return !index.isValid() || nif->getBlock( index, "NiGeometryData" ).isValid();
}

QModelIndex spFO3FixShapeDataName::cast( NifModel * nif, const QModelIndex & index )
{
	if ( index.isValid() && nif->getBlock( index, "NiGeometryData" ).isValid() ) {
		nif->set<int>( index, "Unknown ID", 0 );
	} else {
		// set all blocks
		for ( int n = 0; n < nif->getBlockCount(); n++ ) {
			QModelIndex iBlock = nif->getBlock( n );

			if ( nif->getBlock( iBlock, "NiGeometryData" ).isValid() ) {
				cast( nif, iBlock );
			}
		}
	}

	return index;
}

REGISTER_SPELL( spFO3FixShapeDataName )
