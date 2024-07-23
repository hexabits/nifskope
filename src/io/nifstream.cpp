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

#include "nifstream.h"

#include "data/nifvalue.h"
#include "model/nifmodel.h"

#include "lib/half.h"

#include <QDataStream>
#include <QIODevice>


//! @file nifstream.cpp NIF file I/O

constexpr int CHAR8_STRING_SIZE = 8;

static inline void shortString_prepareForWrite( QByteArray & str )
{
	str.replace( "\\r", "\r" );
	str.replace( "\\n", "\n" );

	if ( str.size() > 254 )
		str.resize( 254 );
}

static inline uint8_t floatToNormByte( float f )
{
	return round( ( (double(f) + 1.0) / 2.0 ) * 255.0 );
}

static inline float normByteToFloat( uint8_t u )
{
	return (double(u) / 255.0) * 2.0 - 1.0;
}

/*
*  NifIStream
*/

void NifIStream::init()
{
	bool32bit = (model->inherits( "NifModel" ) && model->getVersionNumber() <= 0x04000002);
	linkAdjust = (model->inherits( "NifModel" ) && model->getVersionNumber() <  0x0303000D);
	stringAdjust = (model->inherits( "NifModel" ) && model->getVersionNumber() >= 0x14010003);
	bigEndian = false; // set when tFileVersion is read

	dataStream = std::unique_ptr<QDataStream>( new QDataStream( device ) );
	dataStream->setByteOrder( QDataStream::LittleEndian );
	dataStream->setFloatingPointPrecision( QDataStream::SinglePrecision );

	maxLength = 0x8000;
}

bool NifIStream::readSizedString( NifValue & val )
{
	auto valString = static_cast<QString *>(val.val.data);
	if ( !valString )
		return false;

	int32_t len;
	*dataStream >> len;
	if ( len > maxLength || len < 0 ) {
		*valString = tr( "<string too long (0x%1)>" ).arg( len, 0, 16 );
		return false;
	}

	QByteArray byteString = device->read( len );
	if ( byteString.size() != len )
		return false;
	*valString = QString( byteString );
	return true;
}

bool NifIStream::readLineString( QByteArray & outString, int maxLineLength )
{
	outString.reserve( maxLineLength );

	for ( int counter = 0; ; counter++ ) {
		char ch;
		if ( !device->getChar( &ch ) )
			return false;
		if ( ch == '\n')
			break;
		if ( counter >= maxLineLength )
			return false;
		outString.append( ch );
	}

	return true;
}

bool NifIStream::read( NifValue & val )
{
	#define _DEVICE_READ_DATA( data, dataSize ) ( device->read( (char *)(data), (dataSize) ) == (dataSize) )
	#define _DEVICE_READ_ARRAY( arr ) ( device->read( (char *)(arr), sizeof(arr) ) == sizeof(arr) )
	#define _DEVICE_READ_VALUE( val ) ( device->read( (char *)(&(val)), sizeof(val) ) == sizeof(val) )

	// TODO (Gavrant):
	// - What's the point of having 2 different ways of reading streams - dataStream and device?
	// - tHeaderString and tLineString: why the max char number is capped at 79 and 254 respectively? If it has a point, then why those caps are not enforced in ::write() and ::size() below?
	// - tShortString: Why ::write() converts "\\n" to "\n" and "\\r" to "\r", but ::read() doesn't do the opposite?
	// - tBlob: it looks like it never should/could be read from a stream. Return false instead?

	switch ( val.type() ) {
	case NifValue::tBool:
		{
			val.val.u64 = 0;
			if ( bool32bit )
				*dataStream >> val.val.u32;
			else
				*dataStream >> val.val.u08;
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tByte:
		{
			val.val.u64 = 0;
			*dataStream >> val.val.u08;
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tWord:
	case NifValue::tShort:
	case NifValue::tFlags:
	case NifValue::tBlockTypeIndex:
		{
			val.val.u64 = 0;
			*dataStream >> val.val.u16;
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tStringOffset:
	case NifValue::tInt:
	case NifValue::tUInt:
		{
			val.val.u64 = 0;
			*dataStream >> val.val.u32;
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tULittle32:
		{
			val.val.u64 = 0;
			if ( bigEndian ) {
				dataStream->setByteOrder( QDataStream::LittleEndian );
				*dataStream >> val.val.u32;
				dataStream->setByteOrder( QDataStream::BigEndian );
			} else {
				*dataStream >> val.val.u32;
			}
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tInt64:
	case NifValue::tUInt64:
		{
			*dataStream >> val.val.u64;
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tStringIndex:
		{
			val.val.u64 = 0;
			*dataStream >> val.val.u32;
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tLink:
	case NifValue::tUpLink:
		{
			val.val.u64 = 0;
			*dataStream >> val.val.i32;
			if ( dataStream->status() != QDataStream::Ok )
				return false;
			if ( linkAdjust )
				val.val.i32--;
			return true;
		}
	case NifValue::tFloat:
		{
			val.val.u64 = 0;
			*dataStream >> val.val.f32;
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tHfloat:
		{
			uint16_t half;
			*dataStream >> half;
			if ( dataStream->status() != QDataStream::Ok )
				return false;
			val.val.u64 = 0;
			val.val.f32 = halfToFloat( half );
			return true;
		}
	case NifValue::tNormbyte:
		{
			uint8_t v;
			*dataStream >> v;
			if ( dataStream->status() != QDataStream::Ok )
				return false;
			val.val.u64 = 0;
			val.val.f32 = normByteToFloat( v );
			return true;
		}
	case NifValue::tByteVector3:
		{
			auto vec = static_cast<Vector3 *>(val.val.data);
			if ( !vec )
				return false;

			for ( int i = 0; i < 3; i++ ) {
				uint8_t v;
				*dataStream >> v;
				vec->xyz[i] = normByteToFloat( v );
			}
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tUshortVector3:
		{
			auto vec = static_cast<Vector3 *>(val.val.data);
			if ( !vec )
				return false;

			for ( int i = 0; i < 3; i++ ) {
				uint16_t v;
				*dataStream >> v;
				vec->xyz[i] = float( v );
			}
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tHalfVector3:
		{
			auto vec = static_cast<Vector3 *>(val.val.data);
			if ( !vec )
				return false;

			for ( int i = 0; i < 3; i++ ) {
				uint16_t v;
				*dataStream >> v;
				vec->xyz[i] = halfToFloat( v );
			}
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tHalfVector2:
		{
			auto vec = static_cast<Vector2 *>(val.val.data);
			if ( !vec )
				return false;

			for ( int i = 0; i < 2; i++ ) {
				uint16_t v;
				*dataStream >> v;
				vec->xy[i] = halfToFloat( v );
			}
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tVector2:
		{
			auto vec = static_cast<Vector2 *>(val.val.data);
			if ( !vec )
				return false;

			*dataStream >> *vec;
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tVector3:
		{
			auto vec = static_cast<Vector3 *>(val.val.data);
			if ( !vec )
				return false;

			*dataStream >> *vec;
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tVector4:
		{
			auto vec = static_cast<Vector4 *>(val.val.data);
			if ( !vec )
				return false;

			*dataStream >> *vec;
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tTriangle:
		{
			auto tri = static_cast<Triangle *>(val.val.data);
			if ( !tri )
				return false;

			*dataStream >> *tri;
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tQuat:
		{
			auto quat = static_cast<Quat *>(val.val.data);
			if ( !quat )
				return false;

			*dataStream >> *quat;
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tQuatXYZW:
		{
			auto quat = static_cast<Quat *>(val.val.data);
			return quat && _DEVICE_READ_DATA( quat->wxyz + 1, sizeof(float) * 3 ) && _DEVICE_READ_DATA( quat->wxyz, sizeof(float) * 1 );
		}
	case NifValue::tMatrix:
		{
			auto matrix = static_cast<Matrix *>(val.val.data);
			return matrix && _DEVICE_READ_ARRAY( matrix->m );
		}
	case NifValue::tMatrix4:
		{
			auto matrix = static_cast<Matrix4 *>(val.val.data);
			return matrix && _DEVICE_READ_ARRAY( matrix->m );
		}
	case NifValue::tColor3:
		{
			auto color = static_cast<Color3 *>(val.val.data);
			return color && _DEVICE_READ_ARRAY( color->rgb );
		}
	case NifValue::tByteColor4:
		{
			auto color = static_cast<Color4 *>(val.val.data);
			if ( !color )
				return false;

			for ( int i = 0; i < 4; i++ ) {
				uint8_t v;
				*dataStream >> v;
				color->rgba[i] = float( double(v) / 255.0 );
			}
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tColor4:
		{
			auto color = static_cast<Color4 *>(val.val.data);
			if ( !color )
				return false;

			*dataStream >> *color;
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tSizedString:
	case NifValue::tText:
		return readSizedString( val );
	case NifValue::tShortString:
		{
			auto valString = static_cast<QString *>(val.val.data);
			if ( !valString )
				return false;

			uint8_t len;
			if ( !_DEVICE_READ_VALUE( len ) )
				return false;

			QByteArray byteString = device->read( len );
			if ( byteString.size() != len )
				return false;

			*valString = QString::fromLocal8Bit( byteString );
			return true;
		}
	case NifValue::tByteArray:
		{
			auto array = static_cast<QByteArray *>(val.val.data);
			if ( !array )
				return false;

			int32_t len;
			if ( !_DEVICE_READ_VALUE( len ) )
				return false;
			if ( len < 0 )
				return false;

			*array = device->read( len );
			return array->count() == len;
		}
	case NifValue::tStringPalette:
		{
			auto array = static_cast<QByteArray *>(val.val.data);
			if ( !array )
				return false;

			int32_t len;
			if ( !_DEVICE_READ_VALUE( len ) )
				return false;
			if ( len > 0xffff || len < 0 )
				return false;

			*array = device->read( len );
			return _DEVICE_READ_VALUE( len );
		}
	case NifValue::tByteMatrix:
		{
			auto valMatrix = static_cast<ByteMatrix *>(val.val.data);
			if ( !valMatrix )
				return false;

			int32_t size1, size2;
			if ( !_DEVICE_READ_VALUE( size1 ) || !_DEVICE_READ_VALUE( size2 ) )
				return false;
			if ( size1 < 0 || size2 < 0 )
				return false;

			ByteMatrix tmp( size1, size2 );
			int64_t totalSize = int64_t(size1) * int64_t(size2);
			if ( !_DEVICE_READ_DATA( tmp.data(), totalSize ) )
				return false;
			tmp.swap( *valMatrix );
			return true;
		}
	case NifValue::tHeaderString:
		{
			auto valString = static_cast<QString *>(val.val.data);
			if ( !valString )
				return false;

			QByteArray byteString;
			if ( !readLineString( byteString, 79 ) )
				return false;

			uint32_t numVersion = 0;
			// Support NIF versions without "Version" in header string
			// Do for all files for now
			//if ( c == GAMEBRYO_FF || c == NETIMMERSE_FF || c == NEOSTEAM_FF ) {
			device->peek( (char *)&numVersion, 4 );
			// NeoSteam Hack
			if (numVersion == 0x08F35232)
				numVersion = 0x0A010000;
			// Version didn't exist until NetImmerse 4.0
			else if (numVersion < 0x04000000)
				numVersion = 0;
			//}

			*valString = QString( byteString );
			bool result = model->setHeaderString( *valString, numVersion );
			init();
			return result;
		}
	case NifValue::tLineString:
		{
			auto valString = static_cast<QString *>(val.val.data);
			if ( !valString )
				return false;

			QByteArray byteString;
			if ( !readLineString( byteString, 254 ) )
				return false;

			*valString = QString( byteString );
			return true;
		}
	case NifValue::tChar8String:
		{
			auto valString = static_cast<QString *>(val.val.data);
			if ( !valString )
				return false;

			char buffer[CHAR8_STRING_SIZE + 1];
			if ( !_DEVICE_READ_DATA( buffer, CHAR8_STRING_SIZE ) )
				return false;
			buffer[CHAR8_STRING_SIZE] = 0;

			*valString = QString( buffer );
			return true;
		}
	case NifValue::tFileVersion:
		{
			val.val.u64 = 0;
			if ( !_DEVICE_READ_VALUE( val.val.u32 ) )
				return false;

			if ( model->inherits( "NifModel" ) && model->getVersionNumber() >= 0x14000004 ) {
				bool littleEndian;
				device->peek( (char *)&littleEndian, 1 );
				bigEndian = !littleEndian;
				if ( bigEndian )
					dataStream->setByteOrder( QDataStream::BigEndian );
			}

			// hack for neosteam
			if ( val.val.u32 == 0x08F35232 )
				val.val.u32 = 0x0a010000;

			return true;
		}
	case NifValue::tString:
	case NifValue::tFilePath:
		if ( stringAdjust ) {
			val.changeType( NifValue::tStringIndex );
			// val.val.u64 = 0; // val.changeType() above takes care of clearing u64
			return _DEVICE_READ_VALUE( val.val.i32 );
		} else {
			val.changeType( NifValue::tSizedString );
			return readSizedString( val );
		}
	case NifValue::tBSVertexDesc:
		{
			auto d = static_cast<BSVertexDesc *>(val.val.data);
			if ( !d )
				return false;

			*dataStream >> *d;
			return (dataStream->status() == QDataStream::Ok);
		}
	case NifValue::tBlob:
		{
			auto blob = static_cast<QByteArray *>(val.val.data);
			return blob && _DEVICE_READ_DATA( blob->data(), blob->size() );
		}
	case NifValue::tNone:
		return true;
	default:
		Q_ASSERT( 0 );
	}

	return false;
}

void NifIStream::reset()
{
	dataStream->device()->reset();
}


/*
*  NifOStream
*/

void NifOStream::init()
{
	bool32bit = (model->inherits( "NifModel" ) && model->getVersionNumber() <= 0x04000002);
	linkAdjust = (model->inherits( "NifModel" ) && model->getVersionNumber() <  0x0303000D);
	stringAdjust = (model->inherits( "NifModel" ) && model->getVersionNumber() >= 0x14010003);
}

bool NifOStream::write( const NifValue & val )
{
	#define _DEVICE_WRITE_DATA( data, dataSize ) ( device->write( (const char *)(data), (dataSize) ) == (dataSize) )
	#define _DEVICE_WRITE_ARRAY( arr ) ( device->write( (const char *)(arr), sizeof(arr) ) == sizeof(arr) )
	#define _DEVICE_WRITE_VALUE( val ) ( device->write( (const char *)(&(val)), sizeof(val) ) == sizeof(val) )

	switch ( val.type() ) {
	case NifValue::tBool:
		return bool32bit ? _DEVICE_WRITE_VALUE( val.val.u32 ) : _DEVICE_WRITE_VALUE( val.val.u08 );
	case NifValue::tByte:
		return _DEVICE_WRITE_VALUE( val.val.u08 );
	case NifValue::tWord:
	case NifValue::tShort:
	case NifValue::tFlags:
	case NifValue::tBlockTypeIndex:
		return _DEVICE_WRITE_VALUE( val.val.u16 );
	case NifValue::tStringOffset:
	case NifValue::tInt:
	case NifValue::tUInt:
	case NifValue::tULittle32:
	case NifValue::tStringIndex:
		return _DEVICE_WRITE_VALUE( val.val.u32 );
	case NifValue::tInt64:
	case NifValue::tUInt64:
		return _DEVICE_WRITE_VALUE( val.val.u64 );
	case NifValue::tFileVersion:
		{
			uint32_t version = val.val.u32;

			// hack for neosteam
			auto nif = static_cast<const NifModel *>(model);
			if ( nif ) {
				QString headerString = nif->header().child("Header String").value<QString>();
				if ( headerString.startsWith( QStringLiteral("NS") ) )
					version = 0x08F35232;
			}
			
			return _DEVICE_WRITE_VALUE( version );
		}
	case NifValue::tLink:
	case NifValue::tUpLink:
		if ( linkAdjust ) {
			int32_t v = val.val.i32 + 1;
			return _DEVICE_WRITE_VALUE( v );
		} else {
			return _DEVICE_WRITE_VALUE( val.val.i32 );
		}
	case NifValue::tFloat:
		return _DEVICE_WRITE_VALUE( val.val.f32 );
	case NifValue::tHfloat:
		{
			uint16_t half = floatToHalf( val.val.f32 );
			return _DEVICE_WRITE_VALUE( half );
		}
	case NifValue::tNormbyte:
		{
			uint8_t v = floatToNormByte( val.val.f32 );
			return _DEVICE_WRITE_VALUE( v );
		}
	case NifValue::tByteVector3:
		{
			auto vec = static_cast<Vector3 *>(val.val.data);
			if ( !vec )
				return false;

			uint8_t v[3];
			for ( int i = 0; i < 3; i++ )
				v[i] = floatToNormByte( vec->xyz[i] );
			return _DEVICE_WRITE_ARRAY( v );
		}
	case NifValue::tUshortVector3:
		{
			auto vec = static_cast<Vector3 *>(val.val.data);
			if ( !vec )
				return false;

			uint16_t v[3];
			for ( int i = 0; i < 3; i++ )
				v[i] = (uint16_t) round( vec->xyz[i] );
			return _DEVICE_WRITE_ARRAY( v );
		}
	case NifValue::tHalfVector3:
		{
			auto vec = static_cast<Vector3 *>(val.val.data);
			if ( !vec )
				return false;

			uint16_t v[3];
			for ( int i = 0; i < 3; i++)
				v[i] = floatToHalf( vec->xyz[i] );
			return _DEVICE_WRITE_ARRAY( v );
		}
	case NifValue::tHalfVector2:
		{
			auto vec = static_cast<Vector2 *>(val.val.data);
			if ( !vec )
				return false;

			uint16_t v[2];
			v[0] = floatToHalf( vec->xy[0] );
			v[1] = floatToHalf( vec->xy[1] );
			return _DEVICE_WRITE_ARRAY( v );
		}
	case NifValue::tVector3:
		{
			auto vec = static_cast<Vector3 *>(val.val.data);
			return vec && _DEVICE_WRITE_ARRAY( vec->xyz );
		}
	case NifValue::tVector4:
		{
			auto vec = static_cast<Vector4 *>(val.val.data);
			return vec && _DEVICE_WRITE_ARRAY( vec->xyzw ) ;
		}
	case NifValue::tTriangle:
		{
			auto tri = static_cast<Triangle *>(val.val.data);
			return tri && _DEVICE_WRITE_ARRAY( tri->v );
		}
	case NifValue::tQuat:
		{
			auto quat = static_cast<Quat *>(val.val.data);
			return quat && _DEVICE_WRITE_ARRAY( quat->wxyz );
		}
	case NifValue::tQuatXYZW:
		{
			auto quat = static_cast<Quat *>(val.val.data);
			return quat && _DEVICE_WRITE_DATA( quat->wxyz + 1, sizeof(float) * 3 ) && _DEVICE_WRITE_DATA( quat->wxyz, sizeof(float) * 1 );
		}
	case NifValue::tMatrix:
		{
			auto matrix = static_cast<Matrix *>(val.val.data);
			return matrix && _DEVICE_WRITE_ARRAY( matrix->m );
		}
	case NifValue::tMatrix4:
		{
			auto matrix = static_cast<Matrix4 *>(val.val.data);
			return matrix && _DEVICE_WRITE_ARRAY( matrix->m );
		}
	case NifValue::tVector2:
		{
			auto vec = static_cast<Vector2 *>(val.val.data);
			return vec && _DEVICE_WRITE_ARRAY( vec->xy );
		}
	case NifValue::tColor3:
		{
			auto color = static_cast<Color3 *>(val.val.data);
			return color &&_DEVICE_WRITE_ARRAY( color->rgb );
		}
	case NifValue::tByteColor4:
		{
			auto color = static_cast<Color4 *>(val.val.data);
			if ( !color )
				return false;

			uint8_t out[4];
			for ( int i = 0; i < 4; i++ )
				out[i] = std::clamp( round( color->rgba[i] * 255.0 ), 0.0, 255.0 );
			return _DEVICE_WRITE_ARRAY( out );
		}
	case NifValue::tColor4:
		{
			auto color = static_cast<Color4 *>(val.val.data);
			return color && _DEVICE_WRITE_ARRAY( color->rgba );
		}
	case NifValue::tSizedString:
	case NifValue::tText:
	{
			auto valString = static_cast<QString *>(val.val.data);
			if ( !valString )
				return false;

			QByteArray byteString = valString->toLatin1();
			int32_t len = byteString.size();
			return _DEVICE_WRITE_VALUE( len ) && _DEVICE_WRITE_DATA( byteString.constData(), len );
		}
	case NifValue::tShortString:
		{
			auto valString = static_cast<QString *>(val.val.data);
			if ( !valString )
				return false;

			QByteArray byteString = valString->toLocal8Bit();
			shortString_prepareForWrite( byteString );
			uint8_t len = byteString.size() + 1;
			return _DEVICE_WRITE_VALUE( len ) && _DEVICE_WRITE_DATA( byteString.constData(), len );
		}
	case NifValue::tHeaderString:
	case NifValue::tLineString:
		{
			auto valString = static_cast<QString *>(val.val.data);
			if ( !valString )
				return false;

			QByteArray byteString = valString->toLatin1();
			int len = byteString.length();
			return _DEVICE_WRITE_DATA( byteString.constData(), len ) && _DEVICE_WRITE_DATA( "\n", 1 );
		}
	case NifValue::tChar8String:
		{
			auto valString = static_cast<QString *>(val.val.data);
			if ( !valString )
				return false;

			QByteArray byteString = valString->toLatin1();
			int len = std::min( byteString.length(), CHAR8_STRING_SIZE );
			if ( !_DEVICE_WRITE_DATA( byteString.constData(), len ) )
				return false;

			// Pad it to CHAR8_STRING_SIZE bytes
			for ( ; len < CHAR8_STRING_SIZE; len++ ) {
				if ( !_DEVICE_WRITE_DATA( "\0", 1 ) )
					return false;
			}

			return true;
		}
	case NifValue::tByteArray:
		{
			auto array = static_cast<QByteArray *>(val.val.data);
			if ( !array )
				return false;

			int32_t len = array->count();
			return _DEVICE_WRITE_VALUE( len ) && ( device->write( *array ) == len );
		}
	case NifValue::tStringPalette:
		{
			auto array = static_cast<QByteArray *>(val.val.data);
			if ( !array )
				return false;

			int32_t len = array->count();
			if ( len > 0xffff )
				return false;
			return _DEVICE_WRITE_VALUE( len ) && ( device->write( *array ) == len ) && _DEVICE_WRITE_VALUE( len );
		}
	case NifValue::tByteMatrix:
		{
			auto array = static_cast<ByteMatrix *>(val.val.data);
			if ( !array )
				return false;

			int32_t size1 = array->count( 0 );
			int32_t size2 = array->count( 1 );
			int64_t totalSize = int64_t(size1) * int64_t(size2); 

			return _DEVICE_WRITE_VALUE( size1 ) && _DEVICE_WRITE_VALUE( size2 ) && _DEVICE_WRITE_DATA( array->data(), totalSize );
		}
	case NifValue::tString:
	case NifValue::tFilePath:
		{
			if ( stringAdjust ) {
				if ( val.val.u32 < 0x00010000 ) {
					return _DEVICE_WRITE_VALUE( val.val.u32 );
				} else {
					uint32_t value = 0;
					return _DEVICE_WRITE_VALUE( value );
				}
			} else {
				QByteArray byteString;
				if ( val.val.data != 0 )
					byteString = static_cast<QString *>(val.val.data)->toLatin1();
				int32_t len = byteString.size();
				return _DEVICE_WRITE_VALUE( len ) && _DEVICE_WRITE_DATA( byteString.constData(), len );
			}
		}
	case NifValue::tBSVertexDesc:
		{
			auto d = static_cast<BSVertexDesc *>(val.val.data);
			return d && _DEVICE_WRITE_VALUE( d->desc );
		}
	case NifValue::tBlob:
		{
			auto blob = static_cast<QByteArray *>(val.val.data);
			return blob && _DEVICE_WRITE_DATA( blob->data(), blob->size() );
		}
	case NifValue::tNone:
		return true;
	default:
		Q_ASSERT( 0 );
	}

	return false;
}


/*
*  NifSStream
*/

void NifSStream::init()
{
	bool32bit = (model->inherits( "NifModel" ) && model->getVersionNumber() <= 0x04000002);
	stringAdjust = (model->inherits( "NifModel" ) && model->getVersionNumber() >= 0x14010003);
}

int NifSStream::size( const NifValue & val )
{
	switch ( val.type() ) {
	case NifValue::tBool:
		return bool32bit ? 4 : 1;
	case NifValue::tByte:
	case NifValue::tNormbyte:
		return 1;
	case NifValue::tWord:
	case NifValue::tShort:
	case NifValue::tFlags:
	case NifValue::tBlockTypeIndex:
		return 2;
	case NifValue::tStringOffset:
	case NifValue::tInt:
	case NifValue::tUInt:
	case NifValue::tULittle32:
	case NifValue::tStringIndex:
	case NifValue::tFileVersion:
	case NifValue::tLink:
	case NifValue::tUpLink:
	case NifValue::tFloat:
		return 4;
	case NifValue::tInt64:
	case NifValue::tUInt64:
		return 8;
	case NifValue::tHfloat:
		return 2;
	case NifValue::tByteVector3:
		return 1 * 3;
	case NifValue::tUshortVector3:
		return 2 * 3;
	case NifValue::tHalfVector3:
		return 2 * 3;
	case NifValue::tHalfVector2:
		return 2 * 2;
	case NifValue::tVector2:
		return 4 * 2;
	case NifValue::tVector3:
		return 4 * 3;
	case NifValue::tVector4:
		return 4 * 4;
	case NifValue::tTriangle:
		return 2 * 3;
	case NifValue::tQuat:
	case NifValue::tQuatXYZW:
		return 4 * 4;
	case NifValue::tMatrix:
		return 4 * 3 * 3;
	case NifValue::tMatrix4:
		return 4 * 4 * 4;
	case NifValue::tBSVertexDesc:
		return 8;
	case NifValue::tColor3:
		return 4 * 3;
	case NifValue::tByteColor4:
		return 1 * 4;
	case NifValue::tColor4:
		return 4 * 4;
	case NifValue::tSizedString:
	case NifValue::tText:
		{
			auto valString = static_cast<QString *>(val.val.data);
			return 4 + ( valString ? valString->toLatin1().size() : 0 );
		}
	case NifValue::tShortString:
		{
			int len = 0;

			auto valString = static_cast<QString *>(val.val.data);
			if ( valString ) {
				QByteArray byteString = valString->toLatin1();
				shortString_prepareForWrite( byteString );
				len = byteString.size();
			}

			return 1 + len + 1;
		}
	case NifValue::tHeaderString:
	case NifValue::tLineString:
		{
			auto valString = static_cast<QString *>(val.val.data);
			return ( valString ? valString->toLatin1().size() : 0 ) + 1;
		}
	case NifValue::tChar8String:
		return CHAR8_STRING_SIZE;
	case NifValue::tByteArray:
		{
			auto array = static_cast<QByteArray *>(val.val.data);
			return 4 + ( array ? array->count() : 0 );
		}
	case NifValue::tStringPalette:
		{
			auto array = static_cast<QByteArray *>(val.val.data);
			return 4 + ( array ? array->count() : 0 ) + 4;
		}
	case NifValue::tByteMatrix:
		{
			auto array = static_cast<ByteMatrix *>(val.val.data);
			return 4 + 4 + ( array ? array->count() : 0 );
		}
	case NifValue::tString:
	case NifValue::tFilePath:
		if ( stringAdjust ) {
			return 4;
		} else {
			auto valString = static_cast<QString *>(val.val.data);
			return 4 + ( valString ? valString->toLatin1().size() : 0 );
		}
	case NifValue::tBlob:
		{
			auto blob = static_cast<QByteArray *>(val.val.data);
			return blob ? blob->size() : 0;
		}
	case NifValue::tNone:
		return 0;
	default:
		Q_ASSERT( 0 );
	}

	return 0;
}
