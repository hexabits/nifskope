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

#include "fileselect.h"

#include <QAction>
#include <QApplication>
#include <QCompleter>
#include <QContextMenuEvent>
#include <QFileSystemModel>
#include <QFileDialog>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QToolButton>

// how long the visual save/load feedback is visible
#define FEEDBACK_TIME 1200


QAction * FileSelector::completionAction;

CompletionAction::CompletionAction( QObject * parent ) : QAction( "Completion of Filenames", parent )
{
	QSettings cfg;
	setCheckable( true );
	setChecked( cfg.value( "completion of file names", true ).toBool() );

	connect( this, &CompletionAction::toggled, this, &CompletionAction::sltToggled );
}

CompletionAction::~CompletionAction()
{
}

void CompletionAction::sltToggled( bool )
{
	QSettings cfg;
	cfg.setValue( tr( "completion of file names" ), isChecked() );
}

FileSelector::FileSelector( Modes mode, const QString & buttonText, QBoxLayout::Direction dir, QKeySequence keySeq )
	: QWidget(), Mode( mode ), dirmdl( 0 ), completer( 0 )
{
	QBoxLayout * lay = new QBoxLayout( dir, this );
	lay->setMargin( 0 );
	setLayout( lay );

	line = new QLineEdit( this );

	connect( line, &QLineEdit::textEdited, this, &FileSelector::sigEdited );
	connect( line, &QLineEdit::returnPressed , this, &FileSelector::activate );

	action = new QAction( this );
	action->setText( buttonText );
	action->setIconText( buttonText ); // To support browse buttons with names like "..."
	connect( action, &QAction::triggered, this, &FileSelector::browse );

	if ( !keySeq.isEmpty() ) {
		action->setShortcut( keySeq );
	}

	addAction( action );

	QToolButton * button = new QToolButton( this );
	button->setDefaultAction( action );
	button->setFixedHeight( line->sizeHint().height() + 2 ); // Without the "+ 2" the actual height of the button is smaller than line->sizeHint().height()

	lay->addWidget( line );
	lay->addWidget( button );
	
	// setFocusProxy( line );

	line->installEventFilter( this );

	completionAction = new CompletionAction( this );

	connect( completionAction, &QAction::toggled, this, &FileSelector::setCompletionEnabled );
	setCompletionEnabled( completionAction->isChecked() );

	timer = new QTimer( this );
	timer->setSingleShot( true );
	timer->setInterval( FEEDBACK_TIME );
	connect( timer, &QTimer::timeout, this, &FileSelector::rstState );
}

void FileSelector::setCompletionEnabled( bool x )
{
	if ( x && !dirmdl ) {
		QDir::Filters fm;

		switch ( Mode ) {
		case LoadFile:
			fm = QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot;
			break;
		case SaveFile:
			fm = QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot;
			break;
		case Folder:
			fm = QDir::AllDirs | QDir::NoDotAndDotDot;
			break;
		}

		dirmdl = new QFileSystemModel( this );
		dirmdl->setRootPath( QDir::currentPath() );
		dirmdl->setFilter( fm );
		line->setCompleter( completer = new QCompleter( dirmdl, this ) );
	} else if ( !x && dirmdl ) {
		line->setCompleter( 0 );
		delete completer;
		completer = nullptr;
		delete dirmdl;
		dirmdl = nullptr;
	}
}

QString FileSelector::file() const
{
	return line->text();
}

void FileSelector::setFile( const QString & x )
{
	line->setText( QDir::toNativeSeparators( x ) );
}

void FileSelector::setText( const QString & x )
{
	setFile( x );
}

void FileSelector::setState( States s )
{
	State = s;

	if ( State != stNeutral )
		timer->start();
	else
		timer->stop();

	// reload style sheet to refresh State property selector
	// qApp is a macro to the global QApplication instance
	QString styletmp = qApp->styleSheet();
	setStyleSheet( QString() );
	setStyleSheet( styletmp );
}

void FileSelector::rstState()
{
	setState( stNeutral );
}

void FileSelector::replaceText( const QString & x )
{
	line->setCompleter( 0 );
	line->selectAll();
	line->del();
	line->insert( x );
	line->setCompleter( completer );
}

void FileSelector::setFilter( const QStringList & f )
{
	fltr = f;

	if ( dirmdl )
		dirmdl->setNameFilters( fltr );
}

QStringList FileSelector::filter() const
{
	return fltr;
}

QString getFilterFromFilePath( const QStringList & filters, const QString & path )
{
	QString defaultResult;

	if ( filters.count() <= 1 ) // No choice anyway...
		return defaultResult;

	if ( path.isEmpty() )
		return defaultResult;

	QFileInfo finfo( path );
	if ( finfo.isDir() )
		return defaultResult;

	QString fext = finfo.suffix();
	if ( fext.isEmpty() )
		return defaultResult;

	// Look for a filter with "*.<fext>" in its last pair of () brackets
	QString lookupExt = QStringLiteral("*.") + fext;
	for ( const QString & filterEntry : filters ) {
		int iExtStart = filterEntry.lastIndexOf( QStringLiteral("(") );
		if ( iExtStart < 0 )
			continue;
		iExtStart++;

		int iExtEnd = filterEntry.lastIndexOf( QStringLiteral(")") );
		if ( iExtEnd <= iExtStart )
			continue;

		QStringList filterExtensions = filterEntry.mid( iExtStart, iExtEnd - iExtStart ).split( QStringLiteral(" ") );
		for ( const auto & filterExt : filterExtensions ) {
			if ( filterExt.compare( lookupExt, Qt::CaseInsensitive ) == 0 )
				return filterEntry;
		}
	}

	return defaultResult;
}

void FileSelector::browse()
{
	QString newPath;
	QString curPath = file();
	QString startFilter;

	switch ( Mode ) {
	case Folder:
		newPath = QFileDialog::getExistingDirectory( this, tr( "Choose a folder" ), curPath );
		break;
	case LoadFile:
		// Qt uses ;; as separator if multiple types are available
		startFilter = getFilterFromFilePath( fltr, curPath );
		newPath = QFileDialog::getOpenFileName( this, tr( "Choose a file" ), curPath, fltr.join( ";;" ), startFilter.isEmpty() ? nullptr : &startFilter );
		break;
	case SaveFile:
		startFilter = getFilterFromFilePath( fltr, curPath );
		newPath = QFileDialog::getSaveFileName( this, tr( "Choose a file" ), curPath, fltr.join( ";;" ), startFilter.isEmpty() ? nullptr : &startFilter );
		break;
	}

	if ( !newPath.isEmpty() ) {
		line->setText( newPath );
		activate();
	}
}

void FileSelector::activate()
{
	QFileInfo inf( file() );

	switch ( Mode ) {
	case LoadFile:

		if ( !inf.isFile() ) {
			setState( stError );
			return;
		}
		break;
	case SaveFile:

		if ( inf.isDir() ) {
			setState( stError );
			return;
		}
		break;
	case Folder:

		if ( !inf.isDir() ) {
			setState( stError );
			return;
		}
		break;
	}

	emit sigActivated( file() );
}

bool FileSelector::eventFilter( QObject * o, QEvent * e )
{
	if ( o == line && e->type() == QEvent::ContextMenu ) {
		QContextMenuEvent * event = static_cast<QContextMenuEvent *>( e );

		QMenu * menu = line->createStandardContextMenu();
		menu->addSeparator();
		menu->addAction( completionAction );
		menu->exec( event->globalPos() );
		delete menu;
		return true;
	}

	return QWidget::eventFilter( o, e );
}
