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
#include "version.h"
#include "data/nifvalue.h"
#include "model/nifmodel.h"
#include "model/kfmmodel.h"
#include "ui/UiUtils.h"

#include "gamemanager.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDesktopServices>
#include <QDir>
#include <QSettings>
#include <QStack>
#include <QUdpSocket>
#include <QUrl>
#include <QVersionNumber>


QCoreApplication * createApplication( int &argc, char *argv[] )
{
	QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
	// Qt::AA_EnableHighDpiScaling code below was added for the UI to scale according to the Windows settings (primarily needed for high DPI displays).
	// There are some indications that the code is not needed in Qt 6, so this whole subject might be revisited after migrating to Qt 6.
	#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
		QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
		QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
	#endif
	// Iterate over args
	for ( int i = 1; i < argc; ++i ) {
		// -no-gui: start as core app without all the GUI overhead
		if ( !qstrcmp( argv[i], "-no-gui" ) ) {
			return new QCoreApplication( argc, argv );
		}
	}
	return new QApplication( argc, argv );
}


/*
 *  main
 */

void initSettings();

//! The main program
int main( int argc, char * argv[] )
{
	QScopedPointer<QCoreApplication> app( createApplication( argc, argv ) );

	if ( auto a = qobject_cast<QApplication *>(app.data()) ) {
		// The Organization and Application names here define the default path for all QSettings in the app.
		// Any change to them would need a custom code for migrating the settings to the new location on app update.
		// So they must NOT be auto-updated from APP_* macros.
		a->setOrganizationName( "NifTools" );
		a->setApplicationName( "NifSkope 2.0" );

		a->setOrganizationDomain( "niftools.org" );
		a->setApplicationVersion( APP_VER_SHORT );
		#ifdef _DEBUG
		UIUtils::applicationDisplayName = QString( APP_NAME_FULL " - DEBUG" );
		#else
		UIUtils::applicationDisplayName = QString( APP_NAME_FULL );
		#endif

		// Must set current directory or this causes issues with several features
		QDir::setCurrent( qApp->applicationDirPath() );

		// Register message handler
		//qRegisterMetaType<Message>( "Message" );
		qInstallMessageHandler( NifSkope::MessageOutput );
		// freopen("log.txt", "w", stderr);

		// Register types
		qRegisterMetaType<NifValue>( "NifValue" );
		QMetaType::registerComparators<NifValue>();

		// Set locale
		NifSkope::SetAppLocale( QLocale("en") );

		initSettings();

		// Load XML files
		NifModel::loadXML();
		KfmModel::loadXML();

		// Init game manager
		auto mgr = Game::GameManager::get();

		int port = NIFSKOPE_IPC_PORT;

		QStack<QString> fnames;

		// Command Line setup
		QCommandLineParser parser;
		parser.addHelpOption();
		parser.addVersionOption();

		// Add port option
		QCommandLineOption portOption( {"p", "port"}, "Port NifSkope listens on", "port" );
		parser.addOption( portOption );

		// Process options
		parser.process( *a );

		// Override port value
		if ( parser.isSet( portOption ) )
			port = parser.value( portOption ).toInt();

		// Files were passed to NifSkope
		for ( const QString & arg : parser.positionalArguments() ) {
			QString fname = QDir::current().filePath( arg );

			if ( QFileInfo( fname ).exists() ) {
				fnames.push( fname );
			}
		}

		// No files were passed to NifSkope, push empty string
		if ( fnames.isEmpty() ) {
			fnames.push( QString() );
		}

		if ( IPCsocket * ipc = IPCsocket::create( port ) ) {
			//qDebug() << "IPCSocket exec";
			ipc->execCommand( QString( "NifSkope::open %1" ).arg( fnames.pop() ) );

			while ( !fnames.isEmpty() ) {
				IPCsocket::sendCommand( QString( "NifSkope::open %1" ).arg( fnames.pop() ), port );
			}

			return a->exec();
		} else {
			//qDebug() << "IPCSocket send";
			while ( !fnames.isEmpty() ) {
				IPCsocket::sendCommand( QString( "NifSkope::open %1" ).arg( fnames.pop() ), port );
			}
			return 0;
		}
	} else {
		// Future command line batch tools here
	}

	return 0;
}

using MigrateSettingsEntry = QPair<QString, QString>;
using MigrateSettingsList = QVector<MigrateSettingsEntry>;

static const MigrateSettingsList migrate1_1 = {
	{ "auto sanitize", "File/Auto Sanitize" },
	{ "list mode", "UI/List Mode" },
	{ "enable animations", "GLView/Enable Animations" },
	{ "perspective", "GLView/Perspective" },

	{ "Render Settings/Draw Axes", "Settings/Render/General/Startup Defaults/Show Axes" },
	{ "Render Settings/Draw Collision Geometry", "Settings/Render/General/Startup Defaults/Show Collision" },
	{ "Render Settings/Draw Constraints", "Settings/Render/General/Startup Defaults/Show Constraints" },
	{ "Render Settings/Draw Furniture Markers", "Settings/Render/General/Startup Defaults/Show Markers" },
	{ "Render Settings/Draw Nodes", "Settings/Render/General/Startup Defaults/Show Nodes" },
	// { "Render Settings/Enable Shaders", "Settings/Render/General/Use Shaders" },
	{ "Render Settings/Show Hidden Objects", "Settings/Render/General/Startup Defaults/Show Hidden" },
};

static const MigrateSettingsList migrate1_2 = {
	{ "File/Auto Sanitize", "File/Auto Sanitize" },
	{ "UI/List Mode", "UI/List Mode" },
	{ "GLView/Enable Animations", "GLView/Enable Animations" },
	{ "GLView/Perspective", "GLView/Perspective" },

	{ "Render Settings/Draw Axes", "Settings/Render/General/Startup Defaults/Show Axes" },
	{ "Render Settings/Draw Collision Geometry", "Settings/Render/General/Startup Defaults/Show Collision" },
	{ "Render Settings/Draw Constraints", "Settings/Render/General/Startup Defaults/Show Constraints" },
	{ "Render Settings/Draw Furniture Markers", "Settings/Render/General/Startup Defaults/Show Markers" },
	{ "Render Settings/Draw Nodes", "Settings/Render/General/Startup Defaults/Show Nodes" },
	{ "Render Settings/Enable Shaders", "Settings/Render/General/Use Shaders" },
	{ "Render Settings/Show Hidden Objects", "Settings/Render/General/Startup Defaults/Show Hidden" },
};

static void migrateSettings( QSettings & newCfg, const QString & oldCompany, const QString & oldAppName, const MigrateSettingsList & migrateKeys, bool & alreadyMigrated )
{
	QSettings oldCfg( oldCompany, oldAppName );
	if ( !oldCfg.value("Version").isValid() )
		return;

	// The "migrated" thing left in case someone runs an older pre-Dev 10 release of NifSkope 2.0 so it would not copy the settings AGAIN.
	// It could be removed if the settings root changes to something other than "NifSkope 2.0".
	oldCfg.setValue( "migrated", true );

	if ( alreadyMigrated )
		return;
	alreadyMigrated = true;

	auto copyValue = [&oldCfg, &newCfg]( const QString & oldPath, const QString & newPath ) {
		QVariant val = oldCfg.value( oldPath );
		if ( val.isValid() && val.type() != QVariant::ByteArray )
			newCfg.setValue( newPath, val );
	};

	// Copy entire groups
	const QStringList groupsToCopy = { "spells/", "import-export/", "XML Checker/", };
	for ( const auto & key : oldCfg.allKeys() ) {
		for ( const auto & groupToCopy : groupsToCopy ) {
			if ( key.startsWith( groupToCopy, Qt::CaseInsensitive ) ) {
				copyValue( key, key );
				break;
			}
		}
	}

	// Copy stuff from migrateKeys
	for ( const auto & pair : migrateKeys )
		copyValue( pair.first, pair.second );
}

void initSettings()
{
	QSettings cfg;

	QString newCfgVer = APP_VER_SHORT;
	QString oldCfgVer = cfg.value("Version").toString();
	if ( newCfgVer != oldCfgVer ) {
		bool migrated = ( oldCfgVer.length() > 0 );
		migrateSettings( cfg, "NifTools", "NifSkope 1.2", migrate1_2, migrated );
		migrateSettings( cfg, "NifTools", "NifSkope", migrate1_1, migrated );

		cfg.setValue( "Version", newCfgVer );
	}

	// Qt version update
#ifdef QT_NO_DEBUG
	QString newQtVer = QT_VERSION_STR;
	QString oldQtVer = cfg.value("Qt Version").toString();
	if ( newQtVer != oldQtVer ) {
		auto newv = QVersionNumber::fromString( newQtVer );
		auto oldv = QVersionNumber::fromString( oldQtVer );
		if ( newv.majorVersion() != oldv.majorVersion() 
			|| ( oldv.majorVersion() == 5 && oldv.minorVersion() < 7 ) // Gavrant: keeping byte arrays from Qt 5.7.x (Dev 7) and above seems to be rather safe?
		) {
			// Check all keys and delete all QByteArrays to prevent portability problems between Qt versions
			for ( const auto & key : cfg.allKeys() ) {
				if ( cfg.value( key ).type() == QVariant::ByteArray ) {
					// QDebug(QtInfoMsg) << "Removing Qt version-specific settings" << key
					//	<< "while migrating settings from previous version";
					cfg.remove( key );
				}
			}
		}

		cfg.setValue( "Qt Version", newQtVer );
	}
#endif
}


/*
*  IPC socket
*/

IPCsocket * IPCsocket::create( int port )
{
	QUdpSocket * udp = new QUdpSocket();

	if ( udp->bind( QHostAddress( QHostAddress::LocalHost ), port, QUdpSocket::DontShareAddress ) ) {
		IPCsocket * ipc = new IPCsocket( udp );
		QDesktopServices::setUrlHandler( "nif", ipc, "openNif" );
		return ipc;
	}

	return nullptr;
}

void IPCsocket::sendCommand( const QString & cmd, int port )
{
	QUdpSocket udp;
	udp.writeDatagram( (const char *)cmd.data(), cmd.length() * sizeof( QChar ), QHostAddress( QHostAddress::LocalHost ), port );
}

IPCsocket::IPCsocket( QUdpSocket * s ) : QObject(), socket( s )
{
	QObject::connect( socket, &QUdpSocket::readyRead, this, &IPCsocket::processDatagram );
}

IPCsocket::~IPCsocket()
{
	delete socket;
}

void IPCsocket::processDatagram()
{
	while ( socket->hasPendingDatagrams() ) {
		QByteArray data;
		data.resize( socket->pendingDatagramSize() );
		QHostAddress host;
		quint16 port = 0;

		socket->readDatagram( data.data(), data.size(), &host, &port );

		if ( host == QHostAddress( QHostAddress::LocalHost ) && (data.size() % sizeof( QChar )) == 0 ) {
			QString cmd;
			cmd.setUnicode( (QChar *)data.data(), data.size() / sizeof( QChar ) );
			execCommand( cmd );
		}
	}
}

void IPCsocket::execCommand( const QString & cmd )
{
	if ( cmd.startsWith( "NifSkope::open" ) ) {
		openNif( cmd.right( cmd.length() - 15 ) );
	}
}

void IPCsocket::openNif( const QUrl & url )
{
	auto file = url.toString();
	file.remove( 0, 4 );

	openNif( file );
}

void IPCsocket::openNif( const QString & url )
{
	NifSkope::createWindow( url );
}
