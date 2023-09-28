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

#include "nifskope.h"
#include "gl/glscene.h"
#include "glview.h"
#include "model/nifmodel.h"
#include "model/nifproxymodel.h"
#include "ui/widgets/nifview.h"

#include <QDockWidget>
#include <QFileInfo>
#include <QMenu>
#include <QModelIndex>


void exportObj( const NifModel * nif, const QModelIndex & index );
void exportCol( const NifModel * nif, QFileInfo );
void importObj( NifModel * nif, const QModelIndex & index, bool collision = false );
void import3ds( NifModel * nif, const QModelIndex & index );

void exportGltf(const NifModel* nif, const Scene* scene, const QModelIndex& index);

void localImportObj( NifModel * nif, const QModelIndex & index )
{
	importObj( nif, index, false );
}
void localImportObjAsCollision( NifModel * nif, const QModelIndex & index )
{
	importObj( nif, index, true );
}
void localExportObj( const NifModel * nif, [[maybe_unused]] const Scene * scene, const QModelIndex & index )
{
	exportObj( nif, index );
}

bool NifImportExportOption::checkVersion( const NifModel * nif ) const
{
	return BaseModel::checkVersion( nif->getBSVersion(), minBSVersion, maxBSVersion );
}

void NifSkope::addImportExportOption( const QString & shortName, NifImportFuncPtr importFn, NifExportFuncPtr exportFn, quint32 minBSVersion, quint32 maxBSVersion )
{
	NifImportExportOption opt;
	opt.minBSVersion = minBSVersion;
	opt.maxBSVersion = maxBSVersion;

	if ( importFn ) {
		opt.importFn = importFn;
		opt.importAction = mImport->addAction( QString("Import ") + shortName );
	}
	if ( exportFn ) {
		opt.exportFn = exportFn;
		opt.exportAction = mExport->addAction( QString("Export ") + shortName );
	}

	importExportOptions << opt;
}

void NifSkope::fillImportExportMenus()
{
	addImportExportOption( ".OBJ", localImportObj, localExportObj, 0, 172 - 1 );
	addImportExportOption( ".OBJ as Collision", localImportObjAsCollision, nullptr, 1, 172 - 1 );
	addImportExportOption( ".gltf", nullptr, exportGltf, 172 );
	//mExport->addAction( tr( "Export .DAE" ) );
	//mImport->addAction( tr( "Import .3DS" ) );
}

void NifSkope::sltImportExport( QAction * a )
{
	if ( !a ) // Just in case
		return;

	QModelIndex index;

	//Get the currently selected NiBlock index in the list or tree view
	if ( dList->isVisible() ) {
		if ( list->model() == proxy ) {
			index = proxy->mapTo( list->currentIndex() );
		} else if ( list->model() == nif ) {
			index = list->currentIndex();
		}
	} else if ( dTree->isVisible() ) {
		if ( tree->model() == proxy ) {
			index = proxy->mapTo( tree->currentIndex() );
		} else if ( tree->model() == nif ) {
			index = tree->currentIndex();
		}
	}

	for ( const auto & opt : importExportOptions ) {
		if ( a == opt.importAction ) {
			opt.importFn( nif, index );
			break;
		}
		if ( a == opt.exportAction ) {
			opt.exportFn( nif, ogl->scene, index );
			break;
		}
	}
}
