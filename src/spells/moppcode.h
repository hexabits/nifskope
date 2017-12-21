#ifndef MOPPCODE_H
#define MOPPCODE_H

#include "spellbook.h"

// Brief description is deliberately not autolinked to class Spell
/*! \file moppcode.cpp
* \brief Havok MOPP spells
*
* Note that this code only works on the Windows platform due an external
* dependency on the Havok SDK, with which NifMopp.dll is compiled.
*
* Most classes here inherit from the Spell class.
*/

// Need to include headers before testing this
#ifdef Q_OS_WIN32

// This code is only intended to be run with Win32 platform.

extern "C" void * __stdcall SetDllDirectoryA( const char * lpPathName );
extern "C" void * __stdcall LoadLibraryA( const char * lpModuleName );
extern "C" void * __stdcall GetProcAddress ( void * hModule, const char * lpProcName );
extern "C" void __stdcall FreeLibrary( void * lpModule );

class HavokMoppCode
{
private:
	typedef int (__stdcall * fnGenerateMoppCode)( int nVerts, Vector3 const * verts, int nTris, Triangle const * tris );
	typedef int (__stdcall * fnGenerateMoppCodeWithSubshapes)( int nShapes, int const * shapes, int nVerts, Vector3 const * verts, int nTris, Triangle const * tris );
	typedef int (__stdcall * fnRetrieveMoppCode)( int nBuffer, char * buffer );
	typedef int (__stdcall * fnRetrieveMoppScale)( float * value );
	typedef int (__stdcall * fnRetrieveMoppOrigin)( Vector3 * value );

	void * hMoppLib;
	fnGenerateMoppCode GenerateMoppCode;
	fnRetrieveMoppCode RetrieveMoppCode;
	fnRetrieveMoppScale RetrieveMoppScale;
	fnRetrieveMoppOrigin RetrieveMoppOrigin;
	fnGenerateMoppCodeWithSubshapes GenerateMoppCodeWithSubshapes;

public:
	HavokMoppCode() : hMoppLib( 0 ), GenerateMoppCode( 0 ), RetrieveMoppCode( 0 ), RetrieveMoppScale( 0 ),
	RetrieveMoppOrigin( 0 ), GenerateMoppCodeWithSubshapes( 0 )
	{
	}

	~HavokMoppCode()
	{
		if ( hMoppLib )
		FreeLibrary( hMoppLib );
	}

	bool Initialize();

	QByteArray CalculateMoppCode( QVector<Vector3> const & verts, QVector<Triangle> const & tris, Vector3 * origin, float * scale );

	QByteArray CalculateMoppCode( QVector<int> const & subShapesVerts,
		QVector<Vector3> const & verts,
		QVector<Triangle> const & tris,
		Vector3 * origin, float * scale );
	}
	TheHavokCode;

	//! Update Havok MOPP for a given shape
	class spMoppCode final : public Spell
	{
	public:
		QString name() const override final { return Spell::tr( "Update MOPP Code" ); }
		QString page() const override final { return Spell::tr( "Havok" ); }

		bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;
		QModelIndex cast( NifModel * nif, const QModelIndex & iBlock ) override final;
	};

	//! Update MOPP code on all shapes in this model
	class spAllMoppCodes final : public Spell
	{
	public:
		QString name() const override final { return Spell::tr( "Update All MOPP Code" ); }
		QString page() const override final { return Spell::tr( "Batch" ); }

		bool isApplicable( const NifModel * nif, const QModelIndex & idx ) override final;
		QModelIndex cast( NifModel * nif, const QModelIndex & ) override final;
	};

	#endif // MOPPCODE_H

	#endif // Q_OS_WIN32
