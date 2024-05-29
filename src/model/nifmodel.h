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

#ifndef NIFMODEL_H
#define NIFMODEL_H

#include "basemodel.h" // Inherited

#include <QHash>
#include <QReadWriteLock>
#include <QStack>
#include <QStringList>

#include <memory>

class SpellBook;
class QUndoStack;

using NifBlockPtr = std::shared_ptr<NifBlock>;
using SpellBookPtr = std::shared_ptr<SpellBook>;

//! @file nifmodel.h NifModel, NifModelEval


//! Primary string for read failure
const char * const readFail = QT_TR_NOOP( "The NIF file could not be read. See Details for more information." );

//! Secondary string for read failure
const char * const readFailFinal = QT_TR_NOOP( "Failed to load %1" );

template<typename ModelPtr, typename ItemPtr> class NifFieldIteratorSimple;

//! The main data model for the NIF file.
class NifModel final : public BaseModel
{
	Q_OBJECT

	friend class NifXmlHandler;
	friend class NifModelEval;
	friend class NifOStream;
	friend class ArrayUpdateCommand;
	friend NifField;
	friend NifFieldConst;

public:
	NifModel( QObject * parent = nullptr, MsgMode msgMode = BaseModel::MSG_TEST );

	static const NifModel * fromIndex( const QModelIndex & index );
	static const NifModel * fromValidIndex( const QModelIndex & index );

	//! Find and parse the XML file
	static bool loadXML();

	//! When creating NifModels from outside the main thread protect them with a QReadLocker
	static QReadWriteLock XMLlock;

	// QAbstractItemModel

	QVariant data( const QModelIndex & index, int role = Qt::DisplayRole ) const override final;
	bool setData( const QModelIndex & index, const QVariant & value, int role = Qt::EditRole ) override final;
	bool removeRows( int row, int count, const QModelIndex & parent ) override final;

	QModelIndex buddy( const QModelIndex & index ) const override;

	// end QAbstractItemModel

	// BaseModel
	
	void clear() override final;
	bool load( QIODevice & device ) override final;
	bool save( QIODevice & device ) const override final;

	QString getVersion() const override final { return version2string( version ); }
	quint32 getVersionNumber() const override final { return version; }

	// end BaseModel

	//! Load from QIODevice and index
	bool loadIndex( QIODevice & device, const QModelIndex & );
	//! Save to QIODevice and index
	bool saveIndex( QIODevice & device, const QModelIndex & ) const;
	//! Resets the model to its original state in any attached views.
	void reset();

	//! Invalidate only the conditions of the items dependent on this item
	void invalidateDependentConditions( NifItem * item );
	//! Reset all cached conditions of the header
	void invalidateHeaderConditions();

	//! Loads a model and maps links
	bool loadAndMapLinks( QIODevice & device, const QModelIndex &, const QMap<qint32, qint32> & map );
	//! Loads the header from a filename
	bool loadHeaderOnly( const QString & fname );

	//! Returns the the estimated file offset of the model index
	int fileOffset( const QModelIndex & ) const;

	//! Returns the estimated file size of the item
	int blockSize( const NifItem * item ) const;
	//! Returns the estimated file size of the stream
	int blockSize( const NifItem * item, NifSStream & stream ) const;

	/*! Checks if the specified file contains the specified block ID in its header and is of the specified version
	 *
	 * Note that it will not open the full file to look for block types, only the header
	 *
	 * @param filepath	The NIF to check
	 * @param blockId	The block to check for
	 * @param version	The version to check for
	 */
	bool earlyRejection( const QString & filepath, const QString & blockId, quint32 version );

	const NifItem * getHeaderItem() const;
	NifItem * getHeaderItem();
	//! Returns the model index of the NiHeader
	// TODO(Gavrant): try replace it with getHeaderItem
	QModelIndex getHeaderIndex() const;

	//! Updates the header infos ( num blocks etc. )
	void updateHeader();
	//! Extracts the 0x01 separated args from NiDataStream. NiDataStream is the only known block to use RTTI args.
	QString extractRTTIArgs( const QString & RTTIName, NiMesh::DataStreamMetadata & metadata ) const;
	//! Creates the 0x01 separated args for NiDataStream. NiDataStream is the only known block to use RTTI args.
	QString createRTTIName( const QModelIndex & iBlock ) const;
	//! Creates the 0x01 separated args for NiDataStream. NiDataStream is the only known block to use RTTI args.
	QString createRTTIName( const NifItem * block ) const;

	const NifItem * getFooterItem() const;
	NifItem * getFooterItem();

	//! Updates the footer info (num root links etc. )
	void updateFooter();

	//! Set delayed updating of model links
	bool holdUpdates( bool value );

	/*! Item may stop I/O depending on certain children values
	 * @return	Whether or not to test for early I/O skip
	 */
	bool testSkipIO( const NifItem * parent ) const;

	QList<int> getRootLinks() const;
	QList<int> getChildLinks( int block ) const;
	QList<int> getParentLinks( int block ) const;

	/*! Get parent
	 * @return	Parent block number or -1 if there are zero or multiple parents.
	 */
	int getParent( int block ) const;

	/*! Get parent
	 * @return	Parent block number or -1 if there are zero or multiple parents.
	 */
	int getParent( const QModelIndex & index ) const;

	//! Is an item's value a child or parent link?
	bool isLink( const NifItem * item ) const;
	//! Is a model index' value a child or parent link?
	bool isLink( const QModelIndex & index ) const;

	void mapLinks( const QMap<qint32, qint32> & map );

	//! Is name a compound type?
	static bool isCompound( const QString & name );
	//! Is compound of fixed size/condition? (Array optimization)
	static bool isFixedCompound( const QString & name );
	//! Is name an ancestor identifier (<niobject abstract="1">)?
	static bool isAncestor( const QString & name );
	//! Is name a NiBlock identifier (<niobject abstract="0"> or <niobject abstract="1">)?
	bool isAncestorOrNiBlock( const QString & name ) const override final;

	//! Is this version supported?
	static bool isVersionSupported( quint32 );

	static QString version2string( quint32 );
	static quint32 version2number( const QString & );

	//! Check whether the current NIF file version lies in the range [since, until]
	bool checkVersion( quint32 since, quint32 until = 0 ) const;

	quint32 getUserVersion() const { return get<int>( getHeaderItem(), "User Version" ); }
	quint32 getBSVersion() const { return bsVersion; }

	//! Create and return delegate for SpellBook
	static QAbstractItemDelegate * createDelegate( QObject * parent, SpellBookPtr book );

	//! Undo Stack for changes to NifModel
	QUndoStack * undoStack = nullptr;

	// Basic block functions
protected:
	constexpr int firstBlockRow() const;
	int lastBlockRow() const;
	bool isBlockRow( int row ) const;

public:
	//! Get the number of NiBlocks
	int getBlockCount() const;

	//! Get the numerical index (or link) of the block an item belongs to.
	// Return -1 if the item is the root or header or footer or null.
	int getBlockNumber( const NifItem * item ) const;
	//! Get the numerical index (or link) of the block an item belongs to.
	// Return -1 if the item is the root or header or footer or null.
	int getBlockNumber( const QModelIndex & index ) const;
	
	// Checks if blockNum is a valid block number.
	bool isValidBlockNumber( qint32 blockNum ) const;

	//! Insert or append ( row == -1 ) a new NiBlock
	QModelIndex insertNiBlock( const QString & identifier, int row = -1 );
	//! Remove a block from the list
	void removeNiBlock( int blocknum );
	//! Move a block in the list
	void moveNiBlock( int src, int dst );

	//! Returns a list with all known NiXXX ids (<niobject abstract="0">)
	static QStringList allNiBlocks();
	//! Reorders the blocks according to a list of new block numbers
	void reorderBlocks( const QVector<qint32> & order );
	//! Moves all niblocks from this nif to another nif, returns a map which maps old block numbers to new block numbers
	QMap<qint32, qint32> moveAllNiBlocks( NifModel * targetnif, bool update = true );
	//! Convert a block from one type to another
	void convertNiBlock( const QString & identifier, const QModelIndex & index );

	// NifField
public:
	//! Get field object from its model index.
	NifFieldConst field( const QModelIndex & index, bool reportErrors = true ) const;
	//! Get field object from its model index.
	NifField field( const QModelIndex & index, bool reportErrors = true );

	//! Get block (field object) from a link.
	NifFieldConst block( quint32 link ) const;
	//! Get block (field object) from a link.
	NifField block( quint32 link );
	//! Get block (field object) from a model index of its item.
	NifFieldConst block( const QModelIndex & index, bool reportErrors = true ) const;
	//! Get block (field object) from a model index of its item.
	NifField block( const QModelIndex & index, bool reportErrors = true );
private:
	const NifItem * findBlockItemByName( const QString & blockName ) const;
	const NifItem * findBlockItemByName( const QLatin1String & blockName ) const;
public:
	//! Get block (field object) by its name.
	NifFieldConst block( const QString & blockName ) const;
	//! Get block (field object) by its name.
	NifField block( const QString & blockName );
	//! Get block (field object) by its name.
	NifFieldConst block( const QLatin1String & blockName ) const;
	//! Get block (field object) by its name.
	NifField block( const QLatin1String & blockName );
	//! Get block (field object) by its name.
	NifFieldConst block( const char * blockName ) const;
	//! Get block (field object) by its name.
	NifField block( const char * blockName );

	//! Get the header (field object) of the model.
	NifFieldConst header() const;
	//! Get the header (field object) of the model.
	NifField header();

	//! Get the footer (field object) of the model.
	NifFieldConst footer() const;
	//! Get the footer (field object) of the model.
	NifField footer();

	//! Get block iterator of the model for use in for() loops.
	NifFieldIteratorSimple<const NifModel *, const NifItem *> blockIter() const;
	//! Get block iterator of the model for use in for() loops.
	NifFieldIteratorSimple<NifModel *, NifItem *> blockIter();

	// Block item getters
private:
	const NifItem * _getBlockItem( const NifItem * block, const QString & ancestor ) const;
	const NifItem * _getBlockItem( const NifItem * block, const QLatin1String & ancestor ) const;
	const NifItem * _getBlockItem( const NifItem * block, const std::initializer_list<const char *> & ancestors ) const;
	const NifItem * _getBlockItem( const NifItem * block, const QStringList & ancestors ) const;

public:
	//! Get a block NifItem by its number.
	const NifItem * getBlockItem( qint32 link ) const;
	//! Get a block NifItem by its number, with a check that it inherits ancestor.
	const NifItem * getBlockItem( qint32 link, const QString & ancestor ) const;
	//! Get a block NifItem by its number, with a check that it inherits ancestor.
	const NifItem * getBlockItem( qint32 link, const QLatin1String & ancestor ) const;
	//! Get a block NifItem by its number, with a check that it inherits ancestor.
	const NifItem * getBlockItem( qint32 link, const char * ancestor ) const;
	//! Get a block NifItem by its number, with a check that it inherits any of ancestors.
	const NifItem * getBlockItem( qint32 link, const std::initializer_list<const char *> & ancestors ) const;
	//! Get a block NifItem by its number, with a check that it inherits any of ancestors.
	const NifItem * getBlockItem( qint32 link, const QStringList & ancestors ) const;

	//! Get the block NifItem an item belongs to.
	const NifItem * getBlockItem( const NifItem * item ) const;
	//! Get the block NifItem an item belongs to, with a check that it inherits ancestor.
	const NifItem * getBlockItem( const NifItem * item, const QString & ancestor ) const;
	//! Get the block NifItem an item belongs to, with a check that it inherits ancestor.
	const NifItem * getBlockItem( const NifItem * item, const QLatin1String & ancestor ) const;
	//! Get the block NifItem an item belongs to, with a check that it inherits ancestor.
	const NifItem * getBlockItem( const NifItem * item, const char * ancestor ) const;
	//! Get the block NifItem an item belongs to, with a check that it inherits any of ancestors.
	const NifItem * getBlockItem( const NifItem * item, const std::initializer_list<const char *> & ancestors ) const;
	//! Get the block NifItem an item belongs to, with a check that it inherits any of ancestors.
	const NifItem * getBlockItem( const NifItem * item, const QStringList & ancestors ) const;

	//! Get the block NifItem a model index belongs to.
	const NifItem * getBlockItem( const QModelIndex & index ) const;
	//! Get the block NifItem a model index belongs to, with a check that it inherits ancestor.
	const NifItem * getBlockItem( const QModelIndex & index, const QString & ancestor ) const;
	//! Get the block NifItem a model index belongs to, with a check that it inherits ancestor.
	const NifItem * getBlockItem( const QModelIndex & index, const QLatin1String & ancestor ) const;
	//! Get the block NifItem a model index belongs to, with a check that it inherits ancestor.
	const NifItem * getBlockItem( const QModelIndex & index, const char * ancestor ) const;
	//! Get the block NifItem a model index belongs to, with a check that it inherits any of ancestors.
	const NifItem * getBlockItem( const QModelIndex & index, const std::initializer_list<const char *> & ancestors ) const;
	//! Get the block NifItem a model index belongs to, with a check that it inherits any of ancestors.
	const NifItem * getBlockItem( const QModelIndex & index, const QStringList & ancestors ) const;

	//! Get a block NifItem by its number.
	NifItem * getBlockItem( qint32 link );
	//! Get a block NifItem by its number, with a check that it inherits ancestor.
	NifItem * getBlockItem( qint32 link, const QString & ancestor );
	//! Get a block NifItem by its number, with a check that it inherits ancestor.
	NifItem * getBlockItem( qint32 link, const QLatin1String & ancestor );
	//! Get a block NifItem by its number, with a check that it inherits ancestor.
	NifItem * getBlockItem( qint32 link, const char * ancestor );
	//! Get a block NifItem by its number, with a check that it inherits any of ancestors.
	NifItem * getBlockItem( qint32 link, const std::initializer_list<const char *> & ancestors );
	//! Get a block NifItem by its number, with a check that it inherits any of ancestors.
	NifItem * getBlockItem( qint32 link, const QStringList & ancestors );

	//! Get the block NifItem an item belongs to.
	NifItem * getBlockItem( const NifItem * item );
	//! Get the block NifItem an item belongs to, with a check that it inherits ancestor.
	NifItem * getBlockItem( const NifItem * item, const QString & ancestor );
	//! Get the block NifItem an item belongs to, with a check that it inherits ancestor.
	NifItem * getBlockItem( const NifItem * item, const QLatin1String & ancestor );
	//! Get the block NifItem an item belongs to, with a check that it inherits ancestor.
	NifItem * getBlockItem( const NifItem * item, const char * ancestor );
	//! Get the block NifItem an item belongs to, with a check that it inherits any of ancestors.
	NifItem * getBlockItem( const NifItem * item, const std::initializer_list<const char *> & ancestors );
	//! Get the block NifItem an item belongs to, with a check that it inherits any of ancestors.
	NifItem * getBlockItem( const NifItem * item, const QStringList & ancestors );

	//! Get the block NifItem a model index belongs to.
	NifItem * getBlockItem( const QModelIndex & index );
	//! Get the block NifItem a model index belongs to, with a check that it inherits ancestor.
	NifItem * getBlockItem( const QModelIndex & index, const QString & ancestor );
	//! Get the block NifItem a model index belongs to, with a check that it inherits ancestor.
	NifItem * getBlockItem( const QModelIndex & index, const QLatin1String & ancestor );
	//! Get the block NifItem a model index belongs to, with a check that it inherits ancestor.
	NifItem * getBlockItem( const QModelIndex & index, const char * ancestor );
	//! Get the block NifItem a model index belongs to, with a check that it inherits any of ancestors.
	NifItem * getBlockItem( const QModelIndex & index, const std::initializer_list<const char *> & ancestors );
	//! Get the block NifItem a model index belongs to, with a check that it inherits any of ancestors.
	NifItem * getBlockItem( const QModelIndex & index, const QStringList & ancestors );

	// Block index getters
public:
	//! Get a block model index by its number.
	QModelIndex getBlockIndex( qint32 link ) const;
	//! Get a block model index by its number, with a check that it inherits ancestor.
	QModelIndex getBlockIndex( qint32 link, const QString & ancestor ) const;
	//! Get a block model index by its number, with a check that it inherits ancestor.
	QModelIndex getBlockIndex( qint32 link, const QLatin1String & ancestor ) const;
	//! Get a block model index by its number, with a check that it inherits ancestor.
	QModelIndex getBlockIndex( qint32 link, const char * ancestor ) const;
	//! Get a block model index by its number, with a check that it inherits any of ancestors.
	QModelIndex getBlockIndex( qint32 link, const std::initializer_list<const char *> & ancestors ) const;
	//! Get a block model index by its number, with a check that it inherits any of ancestors.
	QModelIndex getBlockIndex( qint32 link, const QStringList & ancestors ) const;

	//! Get the block model index an item belongs to.
	QModelIndex getBlockIndex( const NifItem * item ) const;
	//! Get the block model index an item belongs to, with a check that it inherits ancestor.
	QModelIndex getBlockIndex( const NifItem * item, const QString & ancestor ) const;
	//! Get the block model index an item belongs to, with a check that it inherits ancestor.
	QModelIndex getBlockIndex( const NifItem * item, const QLatin1String & ancestor ) const;
	//! Get the block model index an item belongs to, with a check that it inherits ancestor.
	QModelIndex getBlockIndex( const NifItem * item, const char * ancestor ) const;
	//! Get the block model index an item belongs to, with a check that it inherits any of ancestors.
	QModelIndex getBlockIndex( const NifItem * item, const std::initializer_list<const char *> & ancestors ) const;
	//! Get the block model index an item belongs to, with a check that it inherits any of ancestors.
	QModelIndex getBlockIndex( const NifItem * item, const QStringList & ancestors ) const;

	//! Get the block model index a model index belongs to.
	QModelIndex getBlockIndex( const QModelIndex & index ) const;
	//! Get the block model index a model index belongs to, with a check that it inherits ancestor.
	QModelIndex getBlockIndex( const QModelIndex & index, const QString & ancestor ) const;
	//! Get the block model index a model index belongs to, with a check that it inherits ancestor.
	QModelIndex getBlockIndex( const QModelIndex & index, const QLatin1String & ancestor ) const;
	//! Get the block model index a model index belongs to, with a check that it inherits ancestor.
	QModelIndex getBlockIndex( const QModelIndex & index, const char * ancestor ) const;
	//! Get the block model index a model index belongs to, with a check that it inherits any of ancestors.
	QModelIndex getBlockIndex( const QModelIndex & index, const std::initializer_list<const char *> & ancestors ) const;
	//! Get the block model index a model index belongs to, with a check that it inherits any of ancestors.
	QModelIndex getBlockIndex( const QModelIndex & index, const QStringList & ancestors ) const;

	// isNiBlock
public:
	//! Determine if a value is a NiBlock identifier (<niobject abstract="0">).
	static bool isNiBlock( const QString & name );

	//! Check if a given item is a NiBlock.
	bool isNiBlock( const NifItem * item ) const;
	//! Check if a given item is a NiBlock of testType.
	bool isNiBlock( const NifItem * item, const QString & testType ) const;
	//! Check if a given item is a NiBlock of testType.
	bool isNiBlock( const NifItem * item, const QLatin1String & testType ) const;
	//! Check if a given item is a NiBlock of testType.
	bool isNiBlock( const NifItem * item, const char * testType ) const;
	//! Check if a given item is a NiBlock of one of testTypes.
	bool isNiBlock( const NifItem * item, const std::initializer_list<const char *> & testTypes ) const;
	//! Check if a given item is a NiBlock of one of testTypes.
	bool isNiBlock( const NifItem * item, const QStringList & testTypes ) const;
	//! Check if a given model index is a NiBlock.
	bool isNiBlock( const QModelIndex & index ) const;
	//! Check if a given model index is a NiBlock of testType.
	bool isNiBlock( const QModelIndex & index, const QString & testType ) const;
	//! Check if a given model index is a NiBlock of testType.
	bool isNiBlock( const QModelIndex & index, const QLatin1String & testType ) const;
	//! Check if a given model index is a NiBlock of testType.
	bool isNiBlock( const QModelIndex & index, const char * testType ) const;
	//! Check if a given model index is a NiBlock of one of testTypes.
	bool isNiBlock( const QModelIndex & index, const std::initializer_list<const char *> & testTypes ) const;
	//! Check if a given model index is a NiBlock of one of testTypes.
	bool isNiBlock( const QModelIndex & index, const QStringList & testTypes ) const;

	// Block inheritance
public:
	//! Returns true if blockName inherits ancestor.
	bool inherits( const QString & blockName, const QString & ancestor ) const override final;
	//! Returns true if blockName inherits ancestor.
	bool inherits( const QString & blockName, const QLatin1String & ancestor ) const;
	//! Returns true if blockName inherits ancestor.
	bool inherits( const QString & blockName, const char * ancestor ) const;
	//! Returns true if blockName inherits any of ancestors.
	bool inherits( const QString & blockName, const std::initializer_list<const char *> & ancestors ) const;
	//! Returns true if blockName inherits any of ancestors.
	bool inherits( const QString & blockName, const QStringList & ancestors ) const;

	//! Returns true if the block containing an item inherits ancestor.
	bool blockInherits( const NifItem * item, const QString & ancestor ) const;
	//! Returns true if the block containing an item inherits ancestor.
	bool blockInherits( const NifItem * item, const QLatin1String & ancestor ) const;
	//! Returns true if the block containing an item inherits ancestor.
	bool blockInherits( const NifItem * item, const char * ancestor ) const;
	//! Returns true if the block containing an item inherits any of ancestors.
	bool blockInherits( const NifItem * item, const std::initializer_list<const char *> & ancestors ) const;
	//! Returns true if the block containing an item inherits any of ancestors.
	bool blockInherits( const NifItem * item, const QStringList & ancestors ) const;

	//! Returns true if the block containing a model index inherits ancestor.
	bool blockInherits( const QModelIndex & index, const QString & ancestor ) const;
	//! Returns true if the block containing a model index inherits ancestor.
	bool blockInherits( const QModelIndex & index, const QLatin1String & ancestor ) const;
	//! Returns true if the block containing a model index inherits ancestor.
	bool blockInherits( const QModelIndex & index, const char * ancestor ) const;
	//! Returns true if the block containing a model index inherits any of ancestors.
	bool blockInherits( const QModelIndex & index, const std::initializer_list<const char *> & ancestors ) const;
	//! Returns true if the block containing a model index inherits any of ancestors.
	bool blockInherits( const QModelIndex & index, const QStringList & ancestors ) const;

	// Item value getters
public:
	//! Get the value of an item.
	template <typename T> T get( const NifItem * item ) const;
	//! Get the value of a child item.
	template <typename T> T get( const NifItem * itemParent, int itemIndex ) const;
	//! Get the value of a child item.
	template <typename T> T get( const NifItem * itemParent, const QString & itemName ) const;
	//! Get the value of a child item.
	template <typename T> T get( const NifItem * itemParent, const QLatin1String & itemName ) const;
	//! Get the value of a child item.
	template <typename T> T get( const NifItem * itemParent, const char * itemName ) const;
	//! Get the value of a model index.
	template <typename T> T get( const QModelIndex & index ) const;
	//! Get the value of a child item.
	template <typename T> T get( const QModelIndex & itemParent, int itemIndex ) const;
	//! Get the value of a child item.
	template <typename T> T get( const QModelIndex & itemParent, const QString & itemName ) const;
	//! Get the value of a child item.
	template <typename T> T get( const QModelIndex & itemParent, const QLatin1String & itemName ) const;
	//! Get the value of a child item.
	template <typename T> T get( const QModelIndex & itemParent, const char * itemName ) const;

	// Item value setters
public:
	//! Set the value of an item.
	template <typename T> bool set( NifItem * item, const T & val );
	//! Set the value of a child item.
	template <typename T> bool set( const NifItem * itemParent, int itemIndex, const T & val );
	//! Set the value of a child item.
	template <typename T> bool set( const NifItem * itemParent, const QString & itemName, const T & val );
	//! Set the value of a child item.
	template <typename T> bool set( const NifItem * itemParent, const QLatin1String & itemName, const T & val );
	//! Set the value of a child item.
	template <typename T> bool set( const NifItem * itemParent, const char * itemName, const T & val );
	//! Set the value of a model index.
	template <typename T> bool set( const QModelIndex & index, const T & val );
	//! Set the value of a child item.
	template <typename T> bool set( const QModelIndex & itemParent, int itemIndex, const T & val );
	//! Set the value of a child item.
	template <typename T> bool set( const QModelIndex & itemParent, const QString & itemName, const T & val );
	//! Set the value of a child item.
	template <typename T> bool set( const QModelIndex & itemParent, const QLatin1String & itemName, const T & val );
	//! Set the value of a child item.
	template <typename T> bool set( const QModelIndex & itemParent, const char * itemName, const T & val );

	// String resolving ("get ex")
public:
	//! Get the string value of an item, expanding string indices or subitems if necessary.
	QString resolveString( const NifItem * item ) const;
	//! Get the string value of a child item, expanding string indices or subitems if necessary.
	QString resolveString( const NifItem * itemParent, int itemIndex ) const;
	//! Get the string value of a child item, expanding string indices or subitems if necessary.
	QString resolveString( const NifItem * itemParent, const QString & itemName ) const;
	//! Get the string value of a child item, expanding string indices or subitems if necessary.
	QString resolveString( const NifItem * itemParent, const QLatin1String & itemName ) const;
	//! Get the string value of a child item, expanding string indices or subitems if necessary.
	QString resolveString( const NifItem * itemParent, const char * itemName ) const;
	//! Get the string value of a model index, expanding string indices or subitems if necessary.
	QString resolveString( const QModelIndex & index ) const;
	//! Get the string value of a child item, expanding string indices or subitems if necessary.
	QString resolveString( const QModelIndex & itemParent, int itemIndex ) const;
	//! Get the string value of a child item, expanding string indices or subitems if necessary.
	QString resolveString( const QModelIndex & itemParent, const QString & itemName ) const;
	//! Get the string value of a child item, expanding string indices or subitems if necessary.
	QString resolveString( const QModelIndex & itemParent, const QLatin1String & itemName ) const;
	//! Get the string value of a child item, expanding string indices or subitems if necessary.
	QString resolveString( const QModelIndex & itemParent, const char * itemName ) const;

	// String assigning ("set ex")
public:
	//! Set the string value of an item, updating string indices or subitems if necessary.
	bool assignString( NifItem * item, const QString & string, bool replace = false );
	//! Set the string value of a child item, updating string indices or subitems if necessary.
	bool assignString( const NifItem * itemParent, int itemIndex, const QString & string, bool replace = false );
	//! Set the string value of a child item, updating string indices or subitems if necessary.
	bool assignString( const NifItem * itemParent, const QString & itemName, const QString & string, bool replace = false );
	//! Set the string value of a child item, updating string indices or subitems if necessary.
	bool assignString( const NifItem * itemParent, const QLatin1String & itemName, const QString & string, bool replace = false );
	//! Set the string value of a child item, updating string indices or subitems if necessary.
	bool assignString( const NifItem * itemParent, const char * itemName, const QString & string, bool replace = false );
	//! Set the string value of a model index, updating string indices or subitems if necessary.
	bool assignString( const QModelIndex & index, const QString & string, bool replace = false );
	//! Set the string value of a child item, updating string indices or subitems if necessary.
	bool assignString( const QModelIndex & itemParent, int itemIndex, const QString & string, bool replace = false );
	//! Set the string value of a child item, updating string indices or subitems if necessary.
	bool assignString( const QModelIndex & itemParent, const QString & itemName, const QString & string, bool replace = false );
	//! Set the string value of a child item, updating string indices or subitems if necessary.
	bool assignString( const QModelIndex & itemParent, const QLatin1String & itemName, const QString & string, bool replace = false );
	//! Set the string value of a child item, updating string indices or subitems if necessary.
	bool assignString( const QModelIndex & itemParent, const char * itemName, const QString & string, bool replace = false );

	// Link getters
public:
	//! Return the link value (block number) of an item if it's a valid link, otherwise -1.
	qint32 getLink( const NifItem * item ) const;
	//! Return the link value (block number) of a child item if it's a valid link, otherwise -1.
	qint32 getLink( const NifItem * itemParent, int itemIndex ) const;
	//! Return the link value (block number) of a child item if it's a valid link, otherwise -1.
	qint32 getLink( const NifItem * itemParent, const QString & itemName ) const;
	//! Return the link value (block number) of a child item if it's a valid link, otherwise -1.
	qint32 getLink( const NifItem * itemParent, const QLatin1String & itemName ) const;
	//! Return the link value (block number) of a child item if it's a valid link, otherwise -1.
	qint32 getLink( const NifItem * itemParent, const char * itemName ) const;
	//! Return the link value (block number) of a model index if it's a valid link, otherwise -1.
	qint32 getLink( const QModelIndex & index ) const;
	//! Return the link value (block number) of a child item if it's a valid link, otherwise -1.
	qint32 getLink( const QModelIndex & itemParent, int itemIndex ) const;
	//! Return the link value (block number) of a child item if it's a valid link, otherwise -1.
	qint32 getLink( const QModelIndex & itemParent, const QString & itemName ) const;
	//! Return the link value (block number) of a child item if it's a valid link, otherwise -1.
	qint32 getLink( const QModelIndex & itemParent, const QLatin1String & itemName ) const;
	//! Return the link value (block number) of a child item if it's a valid link, otherwise -1.
	qint32 getLink( const QModelIndex & itemParent, const char * itemName ) const;

	// Link setters
public:
	//! Set the link value (block number) of an item if it's a valid link.
	bool setLink( NifItem * item, qint32 link );
	//! Set the link value (block number) of a child item if it's a valid link.
	bool setLink( const NifItem * itemParent, int itemIndex, qint32 link );
	//! Set the link value (block number) of a child item if it's a valid link.
	bool setLink( const NifItem * itemParent, const QString & itemName, qint32 link );
	//! Set the link value (block number) of a child item if it's a valid link.
	bool setLink( const NifItem * itemParent, const QLatin1String & itemName, qint32 link );
	//! Set the link value (block number) of a child item if it's a valid link.
	bool setLink( const NifItem * itemParent, const char * itemName, qint32 link );
	//! Set the link value (block number) of a model index if it's a valid link.
	bool setLink( const QModelIndex & index, qint32 link );
	//! Set the link value (block number) of a child item if it's a valid link.
	bool setLink( const QModelIndex & itemParent, int itemIndex, qint32 link );
	//! Set the link value (block number) of a child item if it's a valid link.
	bool setLink( const QModelIndex & itemParent, const QString & itemName, qint32 link );
	//! Set the link value (block number) of a child item if it's a valid link.
	bool setLink( const QModelIndex & itemParent, const QLatin1String & itemName, qint32 link );
	//! Set the link value (block number) of a child item if it's a valid link.
	bool setLink( const QModelIndex & itemParent, const char * itemName, qint32 link );

	// Link array getters
public:
	//! Return a QVector of link values (block numbers) of an item if it's a valid link array.
	QVector<qint32> getLinkArray( const NifItem * arrayRootItem ) const;
	//! Return a QVector of link values (block numbers) of a child item if it's a valid link array.
	QVector<qint32> getLinkArray( const NifItem * arrayParent, int arrayIndex ) const;
	//! Return a QVector of link values (block numbers) of a child item if it's a valid link array.
	QVector<qint32> getLinkArray( const NifItem * arrayParent, const QString & arrayName ) const;
	//! Return a QVector of link values (block numbers) of a child item if it's a valid link array.
	QVector<qint32> getLinkArray( const NifItem * arrayParent, const QLatin1String & arrayName ) const;
	//! Return a QVector of link values (block numbers) of a child item if it's a valid link array.
	QVector<qint32> getLinkArray( const NifItem * arrayParent, const char * arrayName ) const;
	//! Return a QVector of link values (block numbers) of a model index if it's a valid link array.
	QVector<qint32> getLinkArray( const QModelIndex & iArray ) const;
	//! Return a QVector of link values (block numbers) of a child item if it's a valid link array.
	QVector<qint32> getLinkArray( const QModelIndex & arrayParent, int arrayIndex ) const;
	//! Return a QVector of link values (block numbers) of a child item if it's a valid link array.
	QVector<qint32> getLinkArray( const QModelIndex & arrayParent, const QString & arrayName ) const;
	//! Return a QVector of link values (block numbers) of a child item if it's a valid link array.
	QVector<qint32> getLinkArray( const QModelIndex & arrayParent, const QLatin1String & arrayName ) const;
	//! Return a QVector of link values (block numbers) of a child item if it's a valid link array.
	QVector<qint32> getLinkArray( const QModelIndex & arrayParent, const char * arrayName ) const;

	// Link array setters
public:
	//! Write a QVector of link values (block numbers) to an item if it's a valid link array.
	// The size of QVector must match the current size of the array.
	bool setLinkArray( NifItem * arrayRootItem, const QVector<qint32> & links );
	//! Write a QVector of link values (block numbers) to a child item if it's a valid link array.
	// The size of QVector must match the current size of the array.
	bool setLinkArray( const NifItem * arrayParent, int arrayIndex, const QVector<qint32> & links );
	//! Write a QVector of link values (block numbers) to a child item if it's a valid link array.
	// The size of QVector must match the current size of the array.
	bool setLinkArray( const NifItem * arrayParent, const QString & arrayName, const QVector<qint32> & links );
	//! Write a QVector of link values (block numbers) to a child item if it's a valid link array.
	// The size of QVector must match the current size of the array.
	bool setLinkArray( const NifItem * arrayParent, const QLatin1String & arrayName, const QVector<qint32> & links );
	//! Write a QVector of link values (block numbers) to a child item if it's a valid link array.
	// The size of QVector must match the current size of the array.
	bool setLinkArray( const NifItem * arrayParent, const char * arrayName, const QVector<qint32> & links );
	//! Write a QVector of link values (block numbers) to a model index if it's a valid link array.
	// The size of QVector must match the current size of the array.
	bool setLinkArray( const QModelIndex & iArray, const QVector<qint32> & links );
	//! Write a QVector of link values (block numbers) to a child item if it's a valid link array.
	// The size of QVector must match the current size of the array.
	bool setLinkArray( const QModelIndex & arrayParent, int arrayIndex, const QVector<qint32> & links );
	//! Write a QVector of link values (block numbers) to a child item if it's a valid link array.
	// The size of QVector must match the current size of the array.
	bool setLinkArray( const QModelIndex & arrayParent, const QString & arrayName, const QVector<qint32> & links );
	//! Write a QVector of link values (block numbers) to a child item if it's a valid link array.
	// The size of QVector must match the current size of the array.
	bool setLinkArray( const QModelIndex & arrayParent, const QLatin1String & arrayName, const QVector<qint32> & links );
	//! Write a QVector of link values (block numbers) to a child item if it's a valid link array.
	// The size of QVector must match the current size of the array.
	bool setLinkArray( const QModelIndex & arrayParent, const char * arrayName, const QVector<qint32> & links );

public slots:
	void updateSettings();

signals:
	void linksChanged();
	void lodSliderChanged( bool ) const;

protected:
	// BaseModel

	bool updateArraySizeImpl( NifItem * array ) override final;
	bool updateByteArraySize( NifItem * array );
	bool updateChildArraySizes( NifItem * parent );

	QString ver2str( quint32 v ) const override final { return version2string( v ); }
	quint32 str2ver( QString s ) const override final { return version2number( s ); }

	//! Get condition value cache NifItem for an item.
	// If the item has no cache NifItem, returns the item itself.
	const NifItem * getConditionCacheItem( const NifItem * item ) const;

	bool evalVersionImpl( const NifItem * item ) const override final;

	bool evalConditionImpl( const NifItem * item ) const override final;

	bool setHeaderString( const QString &, uint ver = 0 ) override final;

	// end BaseModel

	bool loadItem( NifItem * parent, NifIStream & stream );
	bool loadHeader( NifItem * parent, NifIStream & stream );
	bool saveItem( const NifItem * parent, NifOStream & stream ) const;
	bool fileOffset( const NifItem * parent, const NifItem * target, NifSStream & stream, int & ofs ) const;

protected:
	void insertAncestor( NifItem * parent, const QString & identifier, int row = -1 );
	void insertType( NifItem * parent, const NifData & data, int row = -1 );
	NifItem * insertBranch( NifItem * parent, const NifData & data, int row = -1 );

	void updateLinks( int block = -1 );
	void updateLinks( int block, NifItem * parent );
	void checkLinks( int block, QStack<int> & parents );
	void adjustLinks( NifItem * parent, int block, int delta );
	void mapLinks( NifItem * parent, const QMap<qint32, qint32> & map );

	static void updateStrings( NifModel * src, NifModel * tgt, NifItem * item );

	//! NIF file version
	quint32 version;

	QHash<int, QList<int> > childLinks;
	QHash<int, QList<int> > parentLinks;
	QList<int> rootLinks;

	bool lockUpdates;

	enum UpdateType
	{
		utNone   = 0,
		utHeader = 0x1,
		utLinks  = 0x2,
		utFooter = 0x4,
		utAll = 0x7
	};
	UpdateType needUpdates;

	void updateModel( UpdateType value = utAll );

	quint32 bsVersion;
	void cacheBSVersion( const NifItem * headerItem );

	QString topItemRepr( const NifItem * item ) const override final;
	void onItemValueChange( NifItem * item ) override final;

	void invalidateItemConditions( NifItem * item );

	//! Parse the XML file using a NifXmlHandler
	static QString parseXmlDescription( const QString & filename );

	// XML structures
	static QList<quint32> supportedVersions;
	static QHash<QString, NifBlockPtr> compounds;
	static QHash<QString, NifBlockPtr> fixedCompounds;
	static QHash<QString, NifBlockPtr> blocks;
	static QMap<quint32, NifBlockPtr> blockHashes;

private:
	struct Settings
	{
		QString startupVersion;
		int userVersion;
		int userVersion2;
	} cfg;
};


//! Helper class for evaluating condition expressions
class NifModelEval
{
public:
	NifModelEval( const NifModel * model, const NifItem * item );

	QVariant operator()( const QVariant & v ) const;
private:
	const NifModel * model;
	const NifItem * item;
};


//! Begin/end iterator pointer for NifFieldIterator* classes below.
// Iterates a pointer to NifItem pointers, returns NifFieldTemplate<ModelPtr, ItemPtr> on each iteration.
template <typename ModelPtr, typename ItemPtr>
struct NifFieldIterPtr
{
	using iterator_category = std::forward_iterator_tag;
	using difference_type   = std::ptrdiff_t;

	NifFieldIterPtr() = delete;

	NifFieldIterPtr( ItemPtr * ptr) : m_ptr( ptr ) { }

	NifFieldTemplate<ModelPtr, ItemPtr> operator *() const
	{
		return NifFieldTemplate<ModelPtr, ItemPtr>( *m_ptr );
	}

	ItemPtr * operator ->() { return m_ptr; }

	ItemPtr * ptr() const { return m_ptr; }

	NifFieldIterPtr & operator++()
	{
		m_ptr++; return *this;
	}
	NifFieldIterPtr operator++( int )
	{
		NifFieldIterPtr tmp = *this; ++(*this); return tmp;
	}

	friend bool operator == ( const NifFieldIterPtr & a, const NifFieldIterPtr & b )
	{
		return a.ptr() == b.ptr();
	};
	friend bool operator != ( const NifFieldIterPtr & a, const NifFieldIterPtr & b )
	{
		return a.ptr() != b.ptr();
	};

private:
	ItemPtr * m_ptr;
};

//! Begin/end iterator pointer for NifFieldIteratorEval class below.
// Iterates a pointer to NifItem pointers, skipping NifItems with bad conditions, returns NifFieldTemplate<ModelPtr, ItemPtr> on each iteration.
template <typename ModelPtr, typename ItemPtr>
struct NifFieldEvalIterPtr
{
	using iterator_category = std::forward_iterator_tag;
	using difference_type   = std::ptrdiff_t;

	NifFieldEvalIterPtr() = delete;

	NifFieldEvalIterPtr( ItemPtr * ptr, ItemPtr * end ) : m_ptr( ptr ), m_end( end ) { }

	NifFieldTemplate<ModelPtr, ItemPtr> operator *() const
	{
		return NifFieldTemplate<ModelPtr, ItemPtr>( *m_ptr );
	}

	ItemPtr * operator ->() { return m_ptr; }

	ItemPtr * ptr() const { return m_ptr; }

	NifFieldEvalIterPtr & operator++()
	{ 
		if ( m_end ) {
			// Keep incrementing m_ptr until we reach the end or a NifItem with good conditions
			while ( 1 ) {
				m_ptr++;
				if ( m_ptr == m_end || ( *m_ptr )->model()->evalCondition( *m_ptr ) )
					break;
			}
		} else {
			m_ptr++; 
		}
		return *this; 
	}
	NifFieldEvalIterPtr operator++( int )
	{
		NifFieldEvalIterPtr tmp = *this; ++(*this); return tmp;
	}

	friend bool operator == ( const NifFieldEvalIterPtr & a, const NifFieldEvalIterPtr & b )
	{
		return a.ptr() == b.ptr();
	};
	friend bool operator == ( const NifFieldEvalIterPtr & a, const NifFieldIterPtr<ModelPtr, ItemPtr> & b )
	{
		return a.ptr() == b.ptr();
	};
	friend bool operator != ( const NifFieldEvalIterPtr & a, const NifFieldEvalIterPtr & b )
	{
		return a.ptr() != b.ptr();
	};
	friend bool operator != ( const NifFieldEvalIterPtr & a, const NifFieldIterPtr<ModelPtr, ItemPtr> & b )
	{
		return a.ptr() != b.ptr();
	};

private:
	ItemPtr * m_ptr;
	ItemPtr * m_end; // Contains the end pointer if operator ++ needs to evaluate conditions, otherwise nullptr
};

//! Base iterator of child fields.
template<typename ModelPtr, typename ItemPtr>
class NifFieldIteratorBase
{
public:
	NifFieldIteratorBase() = delete;

	NifFieldIteratorBase( ItemPtr item )
	{
		if ( item && item->childCount() > 0 ) {
			m_start = const_cast<ItemPtr *>( const_cast<NifItem *>(item)->children().begin() );
			m_end   = const_cast<ItemPtr *>( const_cast<NifItem *>(item)->children().end() );
		}
	}

	NifFieldIteratorBase( ItemPtr item, int iStart )
	{
		if ( item ) {
			if ( iStart < 0 )
				iStart = 0;

			if ( iStart < item->childCount() ) {
				m_start = const_cast<ItemPtr *>( const_cast<NifItem *>(item)->children().begin() + iStart );
				m_end   = const_cast<ItemPtr *>( const_cast<NifItem *>(item)->children().end() );
			}
		}
	}

	NifFieldIteratorBase( ItemPtr item, int iStart, int iLast )
	{
		if ( item ) {
			if ( iStart < 0 )
				iStart = 0;

			int nChildren = item->childCount();
			int iEnd = ( iLast >= nChildren ) ? nChildren : ( iLast + 1 );

			if ( iStart < iEnd ) {
				ItemPtr * ptr = const_cast<ItemPtr *>( const_cast<NifItem *>(item)->children().begin() );
				m_start = ptr + iStart;
				m_end   = ptr + iEnd;
			}
		}
	}

	NifFieldIterPtr<ModelPtr, ItemPtr> end() const
	{
		return NifFieldIterPtr<ModelPtr, ItemPtr>( m_end );
	}

protected:
	ItemPtr * m_start = nullptr;
	ItemPtr * m_end = nullptr;
};

//! Simple iterator of child fields, without evaluating children's conditions.
template<typename ModelPtr, typename ItemPtr>
class NifFieldIteratorSimple : public NifFieldIteratorBase<ModelPtr, ItemPtr>
{
public:
	using NifFieldIteratorBase<ModelPtr, ItemPtr>::m_start;

	NifFieldIteratorSimple() = delete;

	NifFieldIteratorSimple( ItemPtr item ) : NifFieldIteratorBase<ModelPtr, ItemPtr>( item ) {}
	NifFieldIteratorSimple( ItemPtr item, int iStart ) : NifFieldIteratorBase<ModelPtr, ItemPtr>( item, iStart ) {}
	NifFieldIteratorSimple( ItemPtr item, int iStart, int iLast ) : NifFieldIteratorBase<ModelPtr, ItemPtr>( item, iStart, iLast ) {}

	NifFieldIterPtr<ModelPtr, ItemPtr> begin() const
	{
		return NifFieldIterPtr<ModelPtr, ItemPtr>( m_start ); 
	}
};

//! Iterator of child fields with children's conditions evaluation (skips children with bad conditions).
template<typename ModelPtr, typename ItemPtr>
class NifFieldIteratorEval final : public NifFieldIteratorBase<ModelPtr, ItemPtr>
{
public:
	using NifFieldIteratorBase<ModelPtr, ItemPtr>::m_start;
	using NifFieldIteratorBase<ModelPtr, ItemPtr>::m_end;

	NifFieldIteratorEval() = delete;

	NifFieldIteratorEval( ItemPtr item ) : NifFieldIteratorBase<ModelPtr, ItemPtr>( item )
	{
		initEvalFlag( item );
	}
	NifFieldIteratorEval( ItemPtr item, int iStart ) : NifFieldIteratorBase<ModelPtr, ItemPtr>( item, iStart )
	{
		initEvalFlag( item );
	}
	NifFieldIteratorEval( ItemPtr item, int iStart, int iLast ) : NifFieldIteratorBase<ModelPtr, ItemPtr>( item, iStart, iLast )
	{
		initEvalFlag( item );
	}

	NifFieldEvalIterPtr<ModelPtr, ItemPtr> begin() const
	{
		return NifFieldEvalIterPtr<ModelPtr, ItemPtr>( m_start, m_evalConditions ? m_end : nullptr ); 
	}

protected:
	bool m_evalConditions;

	inline void initEvalFlag( ItemPtr item )
	{
		m_evalConditions = m_start && !item->isArray();
	}
};


//! NifField template class (container for NifItems and proxy between a NifModel and its NifItems).
template <typename ModelPtr, typename ItemPtr> 
class NifFieldTemplate final
{
	friend NifModel;
	friend NifFieldIterPtr<ModelPtr, ItemPtr>;
	friend NifFieldEvalIterPtr<ModelPtr, ItemPtr>;
	friend NifField;
	friend NifFieldConst;

private:
	ItemPtr m_item; // Only NifItems that pass evalCondition check should be here. Otherwise it must be nullptr.

	inline static const QString EMPTY_QSTRING;

	// Constructors
public:
	constexpr NifFieldTemplate() noexcept : m_item( nullptr ) {}
protected:
	NifFieldTemplate( ItemPtr item ) noexcept : m_item( item ) {}

public:
	bool isNull() const noexcept
	{
		return m_item == nullptr;
	}
	bool isValid() const noexcept
	{
		return m_item != nullptr;
	}
	explicit operator bool() const noexcept
	{
		return isValid();
	}

	operator NifFieldConst() const noexcept
	{
		return NifFieldConst( m_item );
	}

	bool operator == ( const NifField & other ) const noexcept
	{
		return m_item == other.m_item;
	}
	bool operator == ( const NifFieldConst & other ) const noexcept
	{
		return m_item == other.m_item;
	}
	bool operator != ( const NifField & other ) const noexcept
	{
		return m_item != other.m_item;
	}
	bool operator != ( const NifFieldConst & other ) const noexcept
	{
		return m_item != other.m_item;
	}

private:
	static inline ModelPtr _model( ItemPtr item )
	{
		return static_cast<ModelPtr>( item->model() );
	}
	inline ModelPtr _model() const
	{
		return _model( m_item );
	}

public:
	//! Get the field's NifModel.
	ModelPtr model() const
	{
		return m_item ? _model() : nullptr;
	}

	//! Get the field as a NifItem pointer.
	ItemPtr item() const noexcept
	{
		return m_item;
	}

	//! Get a model index of the field.
	QModelIndex toIndex( int column = 0 ) const
	{
		return m_item ? _model()->itemToIndex( m_item, column ) : QModelIndex();
	}

	//! Get string representation of the field.
	QString repr() const
	{
		return m_item ? _model()->itemRepr(m_item) : "[NULL]";
	}


	// Child fields

	//! Get a child field by its index.
	NifFieldTemplate child( int childIndex, bool reportErrors = false ) const 
	{
		return NifFieldTemplate( m_item ? _model()->getItem( m_item, childIndex, reportErrors ) : nullptr );
	}
	//! Get a child field by its name.
	NifFieldTemplate child( const QString & childName, bool reportErrors = false ) const
	{
		return NifFieldTemplate( m_item ? _model()->getItem( m_item, childName, reportErrors ) : nullptr );
	}
	//! Get a child field by its name.
	NifFieldTemplate child( const QLatin1String & name, bool reportErrors = false ) const
	{
		return NifFieldTemplate( m_item ? _model()->getItem( m_item, name, reportErrors ) : nullptr );
	}
	//! Get a child field by its name.
	NifFieldTemplate child( const char * name, bool reportErrors = false ) const
	{
		return child( QLatin1String(name), reportErrors );
	}

	//! Get a child field by its index. Same as field(...) with with reportErrors = true.
	NifFieldTemplate operator [] ( int childIndex ) const
	{
		return child( childIndex, true );
	}
	//! Get a child field by its name. Same as field(...) with with reportErrors = true.
	NifFieldTemplate operator [] ( const QString & childName ) const
	{
		return child( childName, true );
	}
	//! Get a child field by its name. Same as field(...) with with reportErrors = true.
	NifFieldTemplate operator [] ( const QLatin1String & childName ) const
	{
		return child( childName, true );
	}
	//! Get a child field by its name. Same as field(...) with with reportErrors = true.
	NifFieldTemplate operator [] ( const char * childName ) const
	{
		return child( QLatin1String(childName), true );
	}

	//! Get the number of child fields.
	int childCount() const
	{
		return m_item ? m_item->childCount() : 0;
	}

	//! Get iterator of child fields for for(...) loops.
	// The iterator returns only the child fields that pass evalCondition check.
	NifFieldIteratorEval<ModelPtr, ItemPtr> iter() const
	{
		return NifFieldIteratorEval<ModelPtr, ItemPtr>( m_item );
	}
	//! Get iterator of child fields for for(...) loops, starting at iStart index.
	// The iterator returns only the child fields that pass evalCondition check.
	NifFieldIteratorEval<ModelPtr, ItemPtr> iter( int iStart ) const
	{
		return NifFieldIteratorEval<ModelPtr, ItemPtr>( m_item, iStart );
	}
	//! Get iterator of child fields for for(...) loops, starting at iStart index and ending at iLast (inclusive).
	// The iterator returns only the child fields that pass evalCondition check.
	NifFieldIteratorEval<ModelPtr, ItemPtr> iter( int iStart, int iLast ) const
	{
		return NifFieldIteratorEval<ModelPtr, ItemPtr>( m_item, iStart, iLast );
	}


	// Item hierarchy

	//! Get the parent field.
	NifFieldTemplate parent() const
	{
		return NifFieldTemplate( m_item ? m_item->parent() : nullptr );
	}

	//! Check if the field is a direct child of the model's root.
	bool isTop() const
	{
		return m_item && _model()->isTopItem(m_item);
	}

	//! Get the top parent (a direct child of the model's root).
	NifFieldTemplate topParent() const
	{
		return NifFieldTemplate( m_item ? _model()->getTopItem( m_item ) : nullptr );
	}

private:
	static inline bool _isDescendantOf( const NifItem * descendant, const NifItem * ancestor )
	{
		return descendant && descendant->isDescendantOf(ancestor);
	}

public:
	//! Check if the field is testAncestor or its child or a child of a child, etc.
	bool isDescendantOf( const NifField & testAncestor ) const
	{
		return _isDescendantOf( m_item, testAncestor.m_item );
	}
	//! Check if the field is testAncestor or its child or a child of a child, etc.
	bool isDescendantOf( const NifFieldConst & testAncestor ) const
	{
		return _isDescendantOf( m_item, testAncestor.m_item );
	}

	//! Check if testDescendant is this field or its child or a child of a child, etc.
	bool isAncestorOf( const NifField & testDescendant ) const
	{
		return _isDescendantOf( testDescendant.m_item, m_item );
	}
	//! Check if testDescendant is this field or its child or a child of a child, etc.
	bool isAncestorOf( const NifFieldConst & testDescendant ) const
	{
		return _isDescendantOf( testDescendant.m_item, m_item );
	}

	//! Get the ancestry level of the field relative to testAncestor.
	// 0 - this field is testAncestor; 1 - the field is a child of testAncestor; 2 - a child of a child, etc.
	// Returns -1 if the field is not a descendant of testAncestor.
	int ancestorLevel( const NifField & testAncestor ) const
	{
		return m_item ? m_item->ancestorLevel( testAncestor.m_item ) : -1;
	}
	//! Get the ancestry level of the field relative to testAncestor.
	// 0 - this field is testAncestor; 1 - the field is a child of testAncestor; 2 - a child of a child, etc.
	// Returns -1 if the field is not a descendant of testAncestor.
	int ancestorLevel( const NifFieldConst & testAncestor ) const
	{
		return m_item ? m_item->ancestorLevel( testAncestor.m_item ) : -1;
	}

	//! Get the ancestor of the field at level testLevel,
	// where levels are: 0 - this field; 1 - the field's parent; 2 - the parent of the parent, etc.
	NifFieldTemplate ancestorAt( int testLevel ) const
	{
		return NifFieldTemplate( m_item ? m_item->ancestorAt( testLevel ) : nullptr );
	}

	//! Check if the field is a NiBlock.
	bool isBlock() const
	{
		return isTop() && _model()->isBlockRow( m_item->row() );
	}

	//! Check if the field is a NiBlock of type testType (w/o inheritance check).
	bool isBlockType( const QString & testType ) const;
	//! Check if the field is a NiBlock of type testType1 or testType2 (w/o inheritance check).
	bool isBlockType( const QString & testType1, const QString & testType2 ) const;
	//! Check if the field is a NiBlock of type testType1 or testType2 or testType3 (w/o inheritance check).
	bool isBlockType( const QString & testType1, const QString & testType2, const QString & testType3 ) const;
	//! Check if the field is a NiBlock of type testType1 or testType2 or testType3 or testType4 (w/o inheritance check).
	bool isBlockType( const QString & testType1, const QString & testType2, const QString & testType3, const QString & testType4 ) const;
	//! Check if the field is a NiBlock of type testType1 or testType2 or testType3 or testType4 or testType5 (w/o inheritance check).
	bool isBlockType( const QString & testType1, const QString & testType2, const QString & testType3, const QString & testType4, const QString & testType5 ) const;
	//! Check if the field is a NiBlock of type testType (w/o inheritance check).
	bool isBlockType( const QLatin1String & testType ) const;
	//! Check if the field is a NiBlock of type testType1 or testType2 (w/o inheritance check).
	bool isBlockType( const QLatin1String & testType1, const QLatin1String & testType2 ) const;
	//! Check if the field is a NiBlock of type testType1 or testType2 or testType3 (w/o inheritance check).
	bool isBlockType( const QLatin1String & testType1, const QLatin1String & testType2, const QLatin1String & testType3 ) const;
	//! Check if the field is a NiBlock of type testType1 or testType2 or testType3 or testType4 (w/o inheritance check).
	bool isBlockType( const QLatin1String & testType1, const QLatin1String & testType2, const QLatin1String & testType3, const QLatin1String & testType4 ) const;
	//! Check if the field is a NiBlock of type testType1 or testType2 or testType3 or testType4 or testType5 (w/o inheritance check).
	bool isBlockType( const QLatin1String & testType1, const QLatin1String & testType2, const QLatin1String & testType3, const QLatin1String & testType4, const QLatin1String & testType5 ) const;
	//! Check if the field is a NiBlock of type testType (w/o inheritance check).
	bool isBlockType( const char * testType ) const;
	//! Check if the field is a NiBlock of type testType1 or testType2 (w/o inheritance check).
	bool isBlockType( const char * testType1, const char * testType2 ) const;
	//! Check if the field is a NiBlock of type testType1 or testType2 or testType3 (w/o inheritance check).
	bool isBlockType( const char * testType1, const char * testType2, const char * testType3 ) const;
	//! Check if the field is a NiBlock of type testType1 or testType2 or testType3 or testType4 (w/o inheritance check).
	bool isBlockType( const char * testType1, const char * testType2, const char * testType3, const char * testType4 ) const;
	//! Check if the field is a NiBlock of type testType1 or testType2 or testType3 or testType4 or testType5 (w/o inheritance check).
	bool isBlockType( const char * testType1, const char * testType2, const char * testType3, const char * testType4, const char * testType5 ) const;
	//! Check if the field is a NiBlock of any of testTypes (w/o inheritance check).
	bool isBlockType( const QStringList & testTypes ) const;

private:
	static bool _inherits( ItemPtr block, const QString & testAncestor )
	{
		return _model( block )->inherits( block->name(), testAncestor );
	}
	static bool _inherits( ItemPtr block, const QLatin1String & testAncestor )
	{
		return _model( block )->inherits( block->name(), testAncestor );
	}
	static bool _inherits( ItemPtr block, const QStringList & testAncestors )
	{
		return _model( block )->inherits( block->name(), testAncestors );
	}

public:

	//! Check if the field is a NiBlock that inherits testAncestor.
	bool inherits( const QString & testAncestor ) const;
	//! Check if the field is a NiBlock that inherits testAncestor1 or testAncestor2.
	bool inherits( const QString & testAncestor1, const QString & testAncestor2 ) const;
	//! Check if the field is a NiBlock that inherits testAncestor1 or testAncestor2 or testAncestor3.
	bool inherits( const QString & testAncestor1, const QString & testAncestor2, const QString & testAncestor3 ) const;
	//! Check if the field is a NiBlock that inherits testAncestor1 or testAncestor2 or testAncestor3 or testAncestor4.
	bool inherits( const QString & testAncestor1, const QString & testAncestor2, const QString & testAncestor3, const QString & testAncestor4 ) const;
	//! Check if the field is a NiBlock that inherits testAncestor1 or testAncestor2 or testAncestor3 or testAncestor4 or testAncestor5.
	bool inherits( const QString & testAncestor1, const QString & testAncestor2, const QString & testAncestor3, const QString & testAncestor4, const QString & testAncestor5 ) const;
	//! Check if the field is a NiBlock that inherits testAncestor.
	bool inherits( const QLatin1String & testAncestor ) const;
	//! Check if the field is a NiBlock that inherits testAncestor1 or testAncestor2.
	bool inherits( const QLatin1String & testAncestor1, const QLatin1String & testAncestor2 ) const;
	//! Check if the field is a NiBlock that inherits testAncestor1 or testAncestor2 or testAncestor3.
	bool inherits( const QLatin1String & testAncestor1, const QLatin1String & testAncestor2, const QLatin1String & testAncestor3 ) const;
	//! Check if the field is a NiBlock that inherits testAncestor1 or testAncestor2 or testAncestor3 or testAncestor4.
	bool inherits( const QLatin1String & testAncestor1, const QLatin1String & testAncestor2, const QLatin1String & testAncestor3, const QLatin1String & testAncestor4 ) const;
	//! Check if the field is a NiBlock that inherits testAncestor1 or testAncestor2 or testAncestor3 or testAncestor4 or testAncestor5.
	bool inherits( const QLatin1String & testAncestor1, const QLatin1String & testAncestor2, const QLatin1String & testAncestor3, const QLatin1String & testAncestor4, const QLatin1String & testAncestor5 ) const;
	//! Check if the field is a NiBlock that inherits testAncestor.
	bool inherits( const char * testAncestor ) const;
	//! Check if the field is a NiBlock that inherits testAncestor1 or testAncestor2.
	bool inherits( const char * testAncestor1, const char * testAncestor2 ) const;
	//! Check if the field is a NiBlock that inherits testAncestor1 or testAncestor2 or testAncestor3.
	bool inherits( const char * testAncestor1, const char * testAncestor2, const char * testAncestor3 ) const;
	//! Check if the field is a NiBlock that inherits testAncestor1 or testAncestor2 or testAncestor3 or testAncestor4.
	bool inherits( const char * testAncestor1, const char * testAncestor2, const char * testAncestor3, const char * testAncestor4 ) const;
	//! Check if the field is a NiBlock that inherits testAncestor1 or testAncestor2 or testAncestor3 or testAncestor4 or testAncestor5.
	bool inherits( const char * testAncestor1, const char * testAncestor2, const char * testAncestor3, const char * testAncestor4, const char * testAncestor5 ) const;
	//! Check if the field is a NiBlock that inherits any of testAncestors.
	bool inherits( const QStringList & testAncestors ) const;

	//! Get the block the field belongs to.
	NifFieldTemplate block() const
	{
		return NifFieldTemplate( m_item ? _model()->getBlockItem( m_item ) : nullptr );
	}
	//! Get the block the field belongs to, with a check that it inherits testAncestor.
	NifFieldTemplate block( const QString & testAncestor ) const;
	//! Get the block the field belongs to, with a check that it inherits testAncestor1 or testAncestor2.
	NifFieldTemplate block( const QString & testAncestor1, const QString & testAncestor2 ) const;
	//! Get the block the field belongs to, with a check that it inherits testAncestor1 or testAncestor2 or testAncestor3.
	NifFieldTemplate block( const QString & testAncestor1, const QString & testAncestor2, const QString & testAncestor3 ) const;
	//! Get the block the field belongs to, with a check that it inherits testAncestor1 or testAncestor2 or testAncestor3 or testAncestor4.
	NifFieldTemplate block( const QString & testAncestor1, const QString & testAncestor2, const QString & testAncestor3, const QString & testAncestor4 ) const;
	//! Get the block the field belongs to, with a check that it inherits testAncestor1 or testAncestor2 or testAncestor3 or testAncestor4 or testAncestor5.
	NifFieldTemplate block( const QString & testAncestor1, const QString & testAncestor2, const QString & testAncestor3, const QString & testAncestor4, const QString & testAncestor5 ) const;
	//! Get the block the field belongs to, with a check that it inherits testAncestor.
	NifFieldTemplate block( const QLatin1String & testAncestor ) const;
	//! Get the block the field belongs to, with a check that it inherits testAncestor1 or testAncestor2.
	NifFieldTemplate block( const QLatin1String & testAncestor1, const QLatin1String & testAncestor2 ) const;
	//! Get the block the field belongs to, with a check that it inherits testAncestor1 or testAncestor2 or testAncestor3.
	NifFieldTemplate block( const QLatin1String & testAncestor1, const QLatin1String & testAncestor2, const QLatin1String & testAncestor3 ) const;
	//! Get the block the field belongs to, with a check that it inherits testAncestor1 or testAncestor2 or testAncestor3 or testAncestor4.
	NifFieldTemplate block( const QLatin1String & testAncestor1, const QLatin1String & testAncestor2, const QLatin1String & testAncestor3, const QLatin1String & testAncestor4 ) const;
	//! Get the block the field belongs to, with a check that it inherits testAncestor1 or testAncestor2 or testAncestor3 or testAncestor4 or testAncestor5.
	NifFieldTemplate block( const QLatin1String & testAncestor1, const QLatin1String & testAncestor2, const QLatin1String & testAncestor3, const QLatin1String & testAncestor4, const QLatin1String & testAncestor5 ) const;
	//! Get the block the field belongs to, with a check that it inherits testAncestor.
	NifFieldTemplate block( const char * testAncestor ) const;
	//! Get the block the field belongs to, with a check that it inherits testAncestor1 or testAncestor2.
	NifFieldTemplate block( const char * testAncestor1, const char * testAncestor2 ) const;
	//! Get the block the field belongs to, with a check that it inherits testAncestor1 or testAncestor2 or testAncestor3.
	NifFieldTemplate block( const char * testAncestor1, const char * testAncestor2, const char * testAncestor3 ) const;
	//! Get the block the field belongs to, with a check that it inherits testAncestor1 or testAncestor2 or testAncestor3 or testAncestor4.
	NifFieldTemplate block( const char * testAncestor1, const char * testAncestor2, const char * testAncestor3, const char * testAncestor4 ) const;
	//! Get the block the field belongs to, with a check that it inherits testAncestor1 or testAncestor2 or testAncestor3 or testAncestor4 or testAncestor5.
	NifFieldTemplate block( const char * testAncestor1, const char * testAncestor2, const char * testAncestor3, const char * testAncestor4, const char * testAncestor5 ) const;
	//! Get the block the field belongs to, with a check that it inherits any of testAncestors.
	NifFieldTemplate block( const QStringList & testAncestors ) const;

	//! Get link to the field (block number) if it's a NiBlock, otherwise returns -1.
	// Same as toBlockNumber().
	qint32 toLink() const
	{
		if ( isTop() ) {
			auto r = m_item->row();
			if ( _model()->isBlockRow( r ) )
				return r - _model()->firstBlockRow();
		}

		return -1;
	}

	//! Get block number (link to the field) if it's a NiBlock, otherwise returns -1.
	// Same as toLink().
	quint32 toBlockNumber() const
	{
		return toLink();
	}

	//! Check if the field is the model's header.
	bool isHeader() const
	{
		return m_item && m_item == _model()->getHeaderItem();
	}

	//! Check if the field is the model's footer.
	bool isFooter() const
	{
		return m_item && m_item == _model()->getFooterItem();
	}


	// NifItem proxy methods

	//! Get the field's row (its index in the parent's children). Returns -1 for null fields.
	int row() const
	{
		return m_item ? m_item->row() : -1;
	}

	//! Get the field's name.
	const QString & name() const
	{
		return m_item ? m_item->name() : EMPTY_QSTRING;
	}
	//! Get the string type of the field data (the "type" attribute in the XML file).
	const QString & strType() const
	{
		return m_item ? m_item->strType() : EMPTY_QSTRING;
	}
	//! Get the field value type (NifValue::Type). Returns NifValue::tNone for null fields.
	NifValue::Type valType() const
	{
		return m_item ? m_item->valueType() : NifValue::tNone;
	}
	//! Get the template type of the field data.
	const QString & templ() const
	{
		return m_item ? m_item->templ() : EMPTY_QSTRING;
	}
	//! Get the argument attribute of the field data.
	const QString & arg() const
	{
		return m_item ? m_item->arg() : EMPTY_QSTRING;
	}
	//! Get the first array length of the field data.
	const QString & arr1() const
	{
		return m_item ? m_item->arr1() : EMPTY_QSTRING;
	}
	//! Get the second array length of the field data.
	const QString & arr2() const
	{
		return m_item ? m_item->arr2() : EMPTY_QSTRING;
	}
	//! Get the condition attribute of the field data.
	const QString & cond() const
	{
		return m_item ? m_item->cond() : EMPTY_QSTRING;
	}
	//! Get the earliest version attribute of the field data. Returns 0 for null fields.
	quint32 ver1() const
	{
		return m_item ? m_item->ver1() : 0;
	}
	//! Get the latest version attribute of the field data. Returns 0 for null fields.
	quint32 ver2() const
	{
		return m_item ? m_item->ver2() : 0;
	}
	//! Get the description text of the field data.
	const QString & text() const
	{
		return m_item ? m_item->text() : EMPTY_QSTRING;
	}

	//! Check if the field's name matches testName.
	bool hasName( const QString & testName ) const;
	//! Check if the field's name matches testName1 or testName2.
	bool hasName( const QString & testName1, const QString & testName2 ) const;
	//! Check if the field's name matches testName1 or testName2 or testName3.
	bool hasName( const QString & testName1, const QString & testName2, const QString & testName3 ) const;
	//! Check if the field's name matches testName1 or testName2 or testName3 or testName4.
	bool hasName( const QString & testName1, const QString & testName2, const QString & testName3, const QString & testName4 ) const;
	//! Check if the field's name matches testName1 or testName2 or testName3 or testName4 or testName5.
	bool hasName( const QString & testName1, const QString & testName2, const QString & testName3, const QString & testName4, const QString & testName5 ) const;
	//! Check if the field's name matches testName.
	bool hasName( const QLatin1String & testName ) const;
	//! Check if the field's name matches testName1 or testName2.
	bool hasName( const QLatin1String & testName1, const QLatin1String & testName2 ) const;
	//! Check if the field's name matches testName1 or testName2 or testName3.
	bool hasName( const QLatin1String & testName1, const QLatin1String & testName2, const QLatin1String & testName3 ) const;
	//! Check if the field's name matches testName1 or testName2 or testName3 or testName4.
	bool hasName( const QLatin1String & testName1, const QLatin1String & testName2, const QLatin1String & testName3, const QLatin1String & testName4 ) const;
	//! Check if the field's name matches testName1 or testName2 or testName3 or testName4 or testName5.
	bool hasName( const QLatin1String & testName1, const QLatin1String & testName2, const QLatin1String & testName3, const QLatin1String & testName4, const QLatin1String & testName5 ) const;
	//! Check if the field's name matches testName.
	bool hasName( const char * testName ) const;
	//! Check if the field's name matches testName1 or testName2.
	bool hasName( const char * testName1, const char * testName2 ) const;
	//! Check if the field's name matches testName1 or testName2 or testName3.
	bool hasName( const char * testName1, const char * testName2, const char * testName3 ) const;
	//! Check if the field's name matches testName1 or testName2 or testName3 or testName4.
	bool hasName( const char * testName1, const char * testName2, const char * testName3, const char * testName4 ) const;
	//! Check if the field's name matches testName1 or testName2 or testName3 or testName4 or testName5.
	bool hasName( const char * testName1, const char * testName2, const char * testName3, const char * testName4, const char * testName5 ) const;

	//! Check if the field's string type matches testType.
	bool hasStrType( const QString & testType ) const;
	//! Check if the field's string type matches testType1 or testType2.
	bool hasStrType( const QString & testType1, const QString & testType2 ) const;
	//! Check if the field's string type matches testType1 or testType2 or testType3.
	bool hasStrType( const QString & testType1, const QString & testType2, const QString & testType3 ) const;
	//! Check if the field's string type matches testType1 or testType2 or testType3 or testType4.
	bool hasStrType( const QString & testType1, const QString & testType2, const QString & testType3, const QString & testType4 ) const;
	//! Check if the field's string type matches testType1 or testType2 or testType3 or testType4 or testType5.
	bool hasStrType( const QString & testType1, const QString & testType2, const QString & testType3, const QString & testType4, const QString & testType5 ) const;
	//! Check if the field's string type matches testType.
	bool hasStrType( const QLatin1String & testType ) const;
	//! Check if the field's string type matches testType1 or testType2.
	bool hasStrType( const QLatin1String & testType1, const QLatin1String & testType2 ) const;
	//! Check if the field's string type matches testType1 or testType2 or testType3.
	bool hasStrType( const QLatin1String & testType1, const QLatin1String & testType2, const QLatin1String & testType3 ) const;
	//! Check if the field's string type matches testType1 or testType2 or testType3 or testType4.
	bool hasStrType( const QLatin1String & testType1, const QLatin1String & testType2, const QLatin1String & testType3, const QLatin1String & testType4 ) const;
	//! Check if the field's string type matches testType1 or testType2 or testType3 or testType4 or testType5.
	bool hasStrType( const QLatin1String & testType1, const QLatin1String & testType2, const QLatin1String & testType3, const QLatin1String & testType4, const QLatin1String & testType5 ) const;
	//! Check if the field's string type matches testType.
	bool hasStrType( const char * testType ) const;
	//! Check if the field's string type matches testType1 or testType2.
	bool hasStrType( const char * testType1, const char * testType2 ) const;
	//! Check if the field's string type matches testType1 or testType2 or testType3.
	bool hasStrType( const char * testType1, const char * testType2, const char * testType3 ) const;
	//! Check if the field's string type matches testType1 or testType2 or testType3 or testType4.
	bool hasStrType( const char * testType1, const char * testType2, const char * testType3, const char * testType4 ) const;
	//! Check if the field's string type matches testType1 or testType2 or testType3 or testType4 or testType5.
	bool hasStrType( const char * testType1, const char * testType2, const char * testType3, const char * testType4, const char * testType5 ) const;

	//! Check if the field's value type (NifValue::Type) matches testType.
	bool hasValType( NifValue::Type testType ) const;
	//! Check if the field's value type (NifValue::Type) matches testType1 or testType2.
	bool hasValType( NifValue::Type testType1, NifValue::Type testType2 ) const;
	//! Check if the field's value type (NifValue::Type) matches testType1 or testType2 or testType3.
	bool hasValType( NifValue::Type testType1, NifValue::Type testType2, NifValue::Type testType3 ) const;
	//! Check if the field's value type (NifValue::Type) matches testType1 or testType2 or testType3 or testType4.
	bool hasValType( NifValue::Type testType1, NifValue::Type testType2, NifValue::Type testType3, NifValue::Type testType4 ) const;
	//! Check if the field's value type (NifValue::Type) matches testType1 or testType2 or testType3 or testType4 or testType5.
	bool hasValType( NifValue::Type testType1, NifValue::Type testType2, NifValue::Type testType3, NifValue::Type testType4, NifValue::Type testType5 ) const;

	//! Check if the type of the field value is a color type (Color3 or Color4 in xml).
	bool isColor() const
	{
		return m_item && m_item->isColor();
	}
	//! Check if the type of the field value is a count.
	bool isCount() const
	{
		return m_item && m_item->isCount();
	}
	//! Check if the type of the field value is a flag type (Flags in xml).
	bool isFlags() const
	{
		return m_item && m_item->isFlags();
	}
	//! Check if the type of the field value is a float type (Float in xml).
	bool isFloat() const
	{
		return m_item && m_item->isFloat();
	}
	//! Check if the type of the field value is of a link type (Ref or Ptr in xml).
	bool isLink() const
	{
		return m_item && m_item->isLink();
	}
	//! Check if the type of the field value is a 3x3 matrix type (Matrix33 in xml).
	bool isMatrix() const
	{
		return m_item && m_item->isMatrix();
	}
	//! Check if the type of the field value is a 4x4 matrix type (Matrix44 in xml).
	bool isMatrix4() const
	{
		return m_item && m_item->isMatrix4();
	}
	//! Check if the type of the field value is a byte matrix.
	bool isByteMatrix() const
	{
		return m_item && m_item->isByteMatrix();
	}
	//! Check if the type of the field value is a quaternion type.
	bool isQuat() const
	{
		return m_item && m_item->isQuat();
	}
	//! Check if the type of the field value is a string type.
	bool isString() const
	{
		return m_item && m_item->isString();
	}
	//! Check if the type of the field value is a Vector 2.
	bool isVector2() const
	{
		return m_item && m_item->isVector2();
	}
	//! Check if the type of the field value is a HalfVector2.
	bool isHalfVector2() const
	{
		return m_item && m_item->isHalfVector2();
	}
	//! Check if the type of the field value is a Vector 3.
	bool isVector3() const
	{
		return m_item && m_item->isVector3();
	}
	//! Check if the type of the field value is a Half Vector3.
	bool isHalfVector3() const
	{
		return m_item && m_item->isHalfVector3();
	}
	//! Check if the type of the field value is a Byte Vector3.
	bool isByteVector3() const
	{
		return m_item && m_item->isByteVector3();
	}
	//! Check if the type of the field value is a Vector 4.
	bool isVector4() const
	{
		return m_item && m_item->isVector4();
	}
	//! Check if the type of the field value is a triangle type.
	bool isTriangle() const
	{
		return m_item && m_item->isTriangle();
	}
	//! Check if the type of the field value is a byte array.
	bool isByteArray() const
	{
		return m_item && m_item->isByteArray();
	}
	//! Check if the type of the field value is a File Version.
	bool isFileVersion() const
	{
		return m_item && m_item->isFileVersion();
	}

	//! Check if the field is abstract the abstract attribute of the field's data.
	bool isAbstract() const
	{
		return m_item && m_item->isAbstract();
	}
	//! Check if the field data is binary. Binary means the data is being treated as one blob.
	bool isBinary() const
	{
		return m_item && m_item->isBinary();
	}
	//! Check if the field data is templated. Templated means the type is dynamic.
	bool isTemplated() const
	{
		return m_item && m_item->isTemplated();
	}
	//! Check if the field data is a compound. Compound means the data type is a compound block.
	bool isCompound() const
	{
		return m_item && m_item->isCompound();
	}
	//! Check if the field data is an array. Array means the data on this row repeats.
	bool isArray() const
	{
		return m_item && m_item->isArray();
	}
	//! Check if the field data is a multi-array. Multi-array means the field's children are also arrays.
	bool isMultiArray() const
	{
		return m_item && m_item->isMultiArray();
	}
	//! Check if the field data is conditionless. Conditionless means no expression evaluation is necessary.
	bool isConditionless() const
	{
		return m_item && m_item->isConditionless();
	}
	//! Does the field data's condition checks only the type of the parent block.
	bool hasTypeCondition() const
	{
		return m_item && m_item->hasTypeCondition();
	}

	// Item value getters/setters

	//! Get the field value.
	template <typename T>
	T value() const
	{
		return m_item ? _model()->get<T>( m_item ) : T();
	}

	//! Set the field value.
	template <typename T>
	bool setValue( const T & val ) const
	{
		return m_item && _model()->set<T>( m_item, val );
	}

	//! Get the child fields' values as a QVector.
	template <typename T>
	QVector<T> array() const
	{
		return m_item ? _model()->getArray<T>( m_item ) : QVector<T>();
	}

	bool updateArraySize() const
	{
		return m_item && _model()->updateArraySize( m_item );
	}

	template <typename T> 
	void setArray( const QVector<T> & array ) const
	{
		if ( m_item )
			_model()->setArray<T>( m_item, array );
	}

	template <typename T>
	void fillArray( const T & val ) const
	{
		if ( m_item )
			_model()->fillArray<T>( m_item, val );
	}

	qint32 link() const
	{
		return m_item ? m_item->getLinkValue() : -1;
	}

	NifFieldTemplate linkBlock() const
	{
		if ( m_item ) {
			auto link = m_item->getLinkValue();
			if ( _model()->isValidBlockNumber( link ) )
				return NifFieldTemplate( _model()->root->child( link + _model()->firstBlockRow() ) );
		}
		return NifFieldTemplate();
	}
	//! Get the link's block, with a check that it inherits testAncestor.
	NifFieldTemplate linkBlock( const QString & testAncestor ) const;
	//! Get the link's block, with a check that it inherits testAncestor1 or testAncestor2.
	NifFieldTemplate linkBlock( const QString & testAncestor1, const QString & testAncestor2 ) const;
	//! Get the link's block, with a check that it inherits testAncestor1 or testAncestor2 or testAncestor3.
	NifFieldTemplate linkBlock( const QString & testAncestor1, const QString & testAncestor2, const QString & testAncestor3 ) const;
	//! Get the link's block, with a check that it inherits testAncestor1 or testAncestor2 or testAncestor3 or testAncestor4.
	NifFieldTemplate linkBlock( const QString & testAncestor1, const QString & testAncestor2, const QString & testAncestor3, const QString & testAncestor4 ) const;
	//! Get the link's block, with a check that it inherits testAncestor1 or testAncestor2 or testAncestor3 or testAncestor4 or testAncestor5.
	NifFieldTemplate linkBlock( const QString & testAncestor1, const QString & testAncestor2, const QString & testAncestor3, const QString & testAncestor4, const QString & testAncestor5 ) const;
	//! Get the link's block, with a check that it inherits testAncestor.
	NifFieldTemplate linkBlock( const QLatin1String & testAncestor ) const;
	//! Get the link's block, with a check that it inherits testAncestor1 or testAncestor2.
	NifFieldTemplate linkBlock( const QLatin1String & testAncestor1, const QLatin1String & testAncestor2 ) const;
	//! Get the link's block, with a check that it inherits testAncestor1 or testAncestor2 or testAncestor3.
	NifFieldTemplate linkBlock( const QLatin1String & testAncestor1, const QLatin1String & testAncestor2, const QLatin1String & testAncestor3 ) const;
	//! Get the link's block, with a check that it inherits testAncestor1 or testAncestor2 or testAncestor3 or testAncestor4.
	NifFieldTemplate linkBlock( const QLatin1String & testAncestor1, const QLatin1String & testAncestor2, const QLatin1String & testAncestor3, const QLatin1String & testAncestor4 ) const;
	//! Get the link's block, with a check that it inherits testAncestor1 or testAncestor2 or testAncestor3 or testAncestor4 or testAncestor5.
	NifFieldTemplate linkBlock( const QLatin1String & testAncestor1, const QLatin1String & testAncestor2, const QLatin1String & testAncestor3, const QLatin1String & testAncestor4, const QLatin1String & testAncestor5 ) const;
	//! Get the link's block, with a check that it inherits testAncestor.
	NifFieldTemplate linkBlock( const char * testAncestor ) const;
	//! Get the link's block, with a check that it inherits testAncestor1 or testAncestor2.
	NifFieldTemplate linkBlock( const char * testAncestor1, const char * testAncestor2 ) const;
	//! Get the link's block, with a check that it inherits testAncestor1 or testAncestor2 or testAncestor3.
	NifFieldTemplate linkBlock( const char * testAncestor1, const char * testAncestor2, const char * testAncestor3 ) const;
	//! Get the link's block, with a check that it inherits testAncestor1 or testAncestor2 or testAncestor3 or testAncestor4.
	NifFieldTemplate linkBlock( const char * testAncestor1, const char * testAncestor2, const char * testAncestor3, const char * testAncestor4 ) const;
	//! Get the link's block, with a check that it inherits testAncestor1 or testAncestor2 or testAncestor3 or testAncestor4 or testAncestor5.
	NifFieldTemplate linkBlock( const char * testAncestor1, const char * testAncestor2, const char * testAncestor3, const char * testAncestor4, const char * testAncestor5 ) const;
	//! Get the link's block, with a check that it inherits any of testAncestors.
	NifFieldTemplate linkBlock( const QStringList & testAncestors ) const;

	bool setLink( qint32 link ) const
	{
		return m_item && _model()->setLink( m_item, link );
	}

	bool clearLink( ) const
	{
		return setLink( -1 );
	}

	template <class NifFieldTemplate>
	bool setLink( const NifFieldTemplate & block ) const
	{
		if ( !m_item )
			return false;

		if ( block.isNull() )
			return _model()->setLink( m_item, -1 );

		if ( block._model() != _model() ) {
			reportError( __func__, QString( "Item \"%1\" belongs to another model." ).arg( block.repr() ) );
			return false;
		}

		auto link = block.toLink();
		if ( link < 0 ) {
			reportError( __func__, QString( "Item \"%1\" is not a NiBlock." ).arg( block.repr() ) );
			return false;
		}

		return _model()->setLink( m_item, link );
	}

	QVector<qint32> linkArray() const
	{
		return m_item ? _model()->getLinkArray( m_item ) : QVector<qint32>();
	}

	bool setLinkArray( QVector<qint32> links ) const
	{
		return m_item && _model()->setLinkArray( m_item, links );
	}

	void reportError( const QString & msg ) const
	{
		if ( m_item )
			_model()->reportError( m_item, msg );
	}
	void reportError( const QString & funcName, const QString & msg ) const
	{
		if ( m_item )
			_model()->reportError( m_item, funcName, msg );
	}
};


// Inlines

inline const NifModel * NifModel::fromIndex( const QModelIndex & index )
{
	return static_cast<const NifModel *>( index.model() ); // qobject_cast
}

inline const NifModel * NifModel::fromValidIndex( const QModelIndex & index )
{
	return index.isValid() ? NifModel::fromIndex( index ) : nullptr;
}

inline QString NifModel::createRTTIName( const QModelIndex & iBlock ) const
{
	return createRTTIName( getItem(iBlock) );
}

inline NifItem * NifModel::getHeaderItem()
{
	return const_cast<NifItem *>( const_cast<const NifModel *>(this)->getHeaderItem() );
}

inline QModelIndex NifModel::getHeaderIndex() const
{
	return itemToIndex( getHeaderItem() );
}

inline NifItem * NifModel::getFooterItem()
{
	return const_cast<NifItem *>( const_cast<const NifModel *>(this)->getFooterItem() );
}

inline QStringList NifModel::allNiBlocks()
{
	QStringList lst;
	for ( NifBlockPtr blk : blocks ) {
		if ( !blk->abstract )
			lst.append( blk->id );
	}
	return lst;
}

inline bool NifModel::isAncestorOrNiBlock( const QString & name ) const
{
	return blocks.contains( name );
}

inline bool NifModel::isNiBlock( const QString & name )
{
	NifBlockPtr blk = blocks.value( name );
	return blk && !blk->abstract;
}

inline bool NifModel::isAncestor( const QString & name )
{
	NifBlockPtr blk = blocks.value( name );
	return blk && blk->abstract;
}

inline bool NifModel::isCompound( const QString & name )
{
	return compounds.contains( name );
}

inline bool NifModel::isFixedCompound( const QString & name )
{
	return fixedCompounds.contains( name );
}

inline bool NifModel::isVersionSupported( quint32 v )
{
	return supportedVersions.contains( v );
}

inline QList<int> NifModel::getRootLinks() const
{
	return rootLinks;
}

inline QList<int> NifModel::getChildLinks( int block ) const
{
	return childLinks.value( block );
}

inline QList<int> NifModel::getParentLinks( int block ) const
{
	return parentLinks.value( block );
}

inline bool NifModel::isLink( const NifItem * item ) const
{
	return item && item->isLink();
}

inline bool NifModel::isLink( const QModelIndex & index ) const
{
	return isLink( getItem(index) );
}

inline bool NifModel::checkVersion( quint32 since, quint32 until ) const
{
	return BaseModel::checkVersion( version, since, until );
}

constexpr inline int NifModel::firstBlockRow() const
{
	return 1; // The fist root's child is always the header
}

inline int NifModel::lastBlockRow() const
{	
	return root->childCount() - 2; // The last root's child is always the footer.
}

inline bool NifModel::isBlockRow( int row ) const
{
	return ( row >= firstBlockRow() && row <= lastBlockRow() );
}

inline int NifModel::getBlockCount() const
{
	return std::max( lastBlockRow() - firstBlockRow() + 1, 0 );
}

inline int NifModel::getBlockNumber( const QModelIndex & index ) const
{
	return getBlockNumber( getItem(index) );
}

inline bool NifModel::isValidBlockNumber( qint32 blockNum ) const
{
	return blockNum >= 0 && blockNum < getBlockCount();
}


// Block item getters

inline const NifItem * NifModel::getBlockItem( qint32 link, const QString & ancestor ) const
{
	return _getBlockItem( getBlockItem(link), ancestor );
}
inline const NifItem * NifModel::getBlockItem( qint32 link, const QLatin1String & ancestor ) const
{
	return _getBlockItem( getBlockItem(link), ancestor );
}
inline const NifItem * NifModel::getBlockItem( qint32 link, const char * ancestor ) const
{
	return _getBlockItem( getBlockItem(link), QLatin1Literal(ancestor) );
}
inline const NifItem * NifModel::getBlockItem( qint32 link, const std::initializer_list<const char *> & ancestors ) const
{
	return _getBlockItem( getBlockItem(link), ancestors );
}
inline const NifItem * NifModel::getBlockItem( qint32 link, const QStringList & ancestors ) const
{
	return _getBlockItem( getBlockItem(link), ancestors );
}

inline const NifItem * NifModel::getBlockItem( const NifItem * item, const QString & ancestor ) const
{
	return _getBlockItem( getBlockItem(item), ancestor );
}
inline const NifItem * NifModel::getBlockItem( const NifItem * item, const QLatin1String & ancestor ) const
{
	return _getBlockItem( getBlockItem(item), ancestor );
}
inline const NifItem * NifModel::getBlockItem( const NifItem * item, const char * ancestor ) const
{
	return _getBlockItem( getBlockItem(item), QLatin1String(ancestor) );
}
inline const NifItem * NifModel::getBlockItem( const NifItem * item, const std::initializer_list<const char *> & ancestors ) const
{
	return _getBlockItem( getBlockItem(item), ancestors );
}
inline const NifItem * NifModel::getBlockItem( const NifItem * item, const QStringList & ancestors ) const
{
	return _getBlockItem( getBlockItem(item), ancestors );
}

inline const NifItem * NifModel::getBlockItem( const QModelIndex & index ) const
{
	return getBlockItem( getItem(index) );
}
inline const NifItem * NifModel::getBlockItem( const QModelIndex & index, const QString & ancestor ) const
{
	return getBlockItem( getItem(index), ancestor );
}
inline const NifItem * NifModel::getBlockItem( const QModelIndex & index, const QLatin1String & ancestor ) const
{
	return getBlockItem( getItem(index), ancestor );
}
inline const NifItem * NifModel::getBlockItem( const QModelIndex & index, const char * ancestor ) const
{
	return getBlockItem( getItem(index), QLatin1String(ancestor) );
}
inline const NifItem * NifModel::getBlockItem( const QModelIndex & index, const std::initializer_list<const char *> & ancestors ) const
{
	return getBlockItem( getItem(index), ancestors );
}
inline const NifItem * NifModel::getBlockItem( const QModelIndex & index, const QStringList & ancestors ) const
{
	return getBlockItem( getItem(index), ancestors );
}

#define _NIFMODEL_NONCONST_GETBLOCKITEM_1(arg) const_cast<NifItem *>( const_cast<const NifModel *>(this)->getBlockItem( arg ) )
#define _NIFMODEL_NONCONST_GETBLOCKITEM_2(arg1, arg2) const_cast<NifItem *>( const_cast<const NifModel *>(this)->getBlockItem( arg1, arg2 ) )

inline NifItem * NifModel::getBlockItem( qint32 link )
{
	return _NIFMODEL_NONCONST_GETBLOCKITEM_1( link );
}
inline NifItem * NifModel::getBlockItem( qint32 link, const QString & ancestor )
{
	return _NIFMODEL_NONCONST_GETBLOCKITEM_2( link, ancestor );
}
inline NifItem * NifModel::getBlockItem( qint32 link, const QLatin1String & ancestor )
{
	return _NIFMODEL_NONCONST_GETBLOCKITEM_2( link, ancestor );
}
inline NifItem * NifModel::getBlockItem( qint32 link, const char * ancestor )
{
	return _NIFMODEL_NONCONST_GETBLOCKITEM_2( link, QLatin1String(ancestor) );
}
inline NifItem * NifModel::getBlockItem( qint32 link, const std::initializer_list<const char *> & ancestors )
{
	return _NIFMODEL_NONCONST_GETBLOCKITEM_2( link, ancestors );
}
inline NifItem * NifModel::getBlockItem( qint32 link, const QStringList & ancestors )
{
	return _NIFMODEL_NONCONST_GETBLOCKITEM_2( link, ancestors );
}

inline NifItem * NifModel::getBlockItem( const NifItem * item )
{
	return _NIFMODEL_NONCONST_GETBLOCKITEM_1( item );
}
inline NifItem * NifModel::getBlockItem( const NifItem * item, const QString & ancestor )
{
	return _NIFMODEL_NONCONST_GETBLOCKITEM_2( item, ancestor );
}
inline NifItem * NifModel::getBlockItem( const NifItem * item, const QLatin1String & ancestor )
{
	return _NIFMODEL_NONCONST_GETBLOCKITEM_2( item, ancestor );
}
inline NifItem * NifModel::getBlockItem( const NifItem * item, const char * ancestor )
{
	return _NIFMODEL_NONCONST_GETBLOCKITEM_2( item, QLatin1String(ancestor) );
}
inline NifItem * NifModel::getBlockItem( const NifItem * item, const std::initializer_list<const char *> & ancestors )
{
	return _NIFMODEL_NONCONST_GETBLOCKITEM_2( item, ancestors );
}
inline NifItem * NifModel::getBlockItem( const NifItem * item, const QStringList & ancestors )
{
	return _NIFMODEL_NONCONST_GETBLOCKITEM_2( item, ancestors );
}

inline NifItem * NifModel::getBlockItem( const QModelIndex & index )
{
	return _NIFMODEL_NONCONST_GETBLOCKITEM_1( index );
}
inline NifItem * NifModel::getBlockItem( const QModelIndex & index, const QString & ancestor )
{
	return _NIFMODEL_NONCONST_GETBLOCKITEM_2( index, ancestor );
}
inline NifItem * NifModel::getBlockItem( const QModelIndex & index, const QLatin1String & ancestor )
{
	return _NIFMODEL_NONCONST_GETBLOCKITEM_2( index, ancestor );
}
inline NifItem * NifModel::getBlockItem( const QModelIndex & index, const char * ancestor )
{
	return _NIFMODEL_NONCONST_GETBLOCKITEM_2( index, QLatin1String(ancestor) );
}
inline NifItem * NifModel::getBlockItem( const QModelIndex & index, const std::initializer_list<const char *> & ancestors )
{
	return _NIFMODEL_NONCONST_GETBLOCKITEM_2( index, ancestors );
}
inline NifItem * NifModel::getBlockItem( const QModelIndex & index, const QStringList & ancestors )
{
	return _NIFMODEL_NONCONST_GETBLOCKITEM_2( index, ancestors );
}


// Block index getters

#define _NIFMODEL_GETBLOCKINDEX_1(arg) itemToIndex( getBlockItem( arg ) )
#define _NIFMODEL_GETBLOCKINDEX_2(arg1, arg2) itemToIndex( getBlockItem( arg1, arg2 ) )

inline QModelIndex NifModel::getBlockIndex( qint32 link ) const
{
	return _NIFMODEL_GETBLOCKINDEX_1( link );
}
inline QModelIndex NifModel::getBlockIndex( qint32 link, const QString & ancestor ) const
{
	return _NIFMODEL_GETBLOCKINDEX_2( link, ancestor );
}
inline QModelIndex NifModel::getBlockIndex( qint32 link, const QLatin1String & ancestor ) const
{
	return _NIFMODEL_GETBLOCKINDEX_2( link, ancestor );
}
inline QModelIndex NifModel::getBlockIndex( qint32 link, const char * ancestor ) const
{
	return _NIFMODEL_GETBLOCKINDEX_2( link, QLatin1String(ancestor) );
}
inline QModelIndex NifModel::getBlockIndex( qint32 link, const std::initializer_list<const char *> & ancestors ) const
{
	return _NIFMODEL_GETBLOCKINDEX_2( link, ancestors );
}
inline QModelIndex NifModel::getBlockIndex( qint32 link, const QStringList & ancestors ) const
{
	return _NIFMODEL_GETBLOCKINDEX_2( link, ancestors );
}

inline QModelIndex NifModel::getBlockIndex( const NifItem * item ) const
{
	return _NIFMODEL_GETBLOCKINDEX_1( item );
}
inline QModelIndex NifModel::getBlockIndex( const NifItem * item, const QString & ancestor ) const
{
	return _NIFMODEL_GETBLOCKINDEX_2( item, ancestor );
}
inline QModelIndex NifModel::getBlockIndex( const NifItem * item, const QLatin1String & ancestor ) const
{
	return _NIFMODEL_GETBLOCKINDEX_2( item, ancestor );
}
inline QModelIndex NifModel::getBlockIndex( const NifItem * item, const char * ancestor ) const
{
	return _NIFMODEL_GETBLOCKINDEX_2( item, QLatin1String(ancestor) );
}
inline QModelIndex NifModel::getBlockIndex( const NifItem * item, const std::initializer_list<const char *> & ancestors ) const
{
	return _NIFMODEL_GETBLOCKINDEX_2( item, ancestors );
}
inline QModelIndex NifModel::getBlockIndex( const NifItem * item, const QStringList & ancestors ) const
{
	return _NIFMODEL_GETBLOCKINDEX_2( item, ancestors );
}

inline QModelIndex NifModel::getBlockIndex( const QModelIndex & index ) const
{
	return _NIFMODEL_GETBLOCKINDEX_1( index );
}
inline QModelIndex NifModel::getBlockIndex( const QModelIndex & index, const QString & ancestor ) const
{
	return _NIFMODEL_GETBLOCKINDEX_2( index, ancestor );
}
inline QModelIndex NifModel::getBlockIndex( const QModelIndex & index, const QLatin1String & ancestor ) const
{
	return _NIFMODEL_GETBLOCKINDEX_2( index, ancestor );
}
inline QModelIndex NifModel::getBlockIndex( const QModelIndex & index, const char * ancestor ) const
{
	return _NIFMODEL_GETBLOCKINDEX_2( index, QLatin1String(ancestor) );
}
inline QModelIndex NifModel::getBlockIndex( const QModelIndex & index, const std::initializer_list<const char *> & ancestors ) const
{
	return _NIFMODEL_GETBLOCKINDEX_2( index, ancestors );
}
inline QModelIndex NifModel::getBlockIndex( const QModelIndex & index, const QStringList & ancestors ) const
{
	return _NIFMODEL_GETBLOCKINDEX_2( index, ancestors );
}


// isNiBlock

inline bool NifModel::isNiBlock( const NifItem * item ) const
{
	return isTopItem( item ) && isBlockRow( item->row() );
}
inline bool NifModel::isNiBlock( const NifItem * item, const QString & testType ) const
{
	return isNiBlock(item) && item->hasName(testType);
}
inline bool NifModel::isNiBlock( const NifItem * item, const QLatin1String & testType ) const
{
	return isNiBlock(item) && item->hasName(testType);
}
inline bool NifModel::isNiBlock( const NifItem * item, const char * testType ) const
{
	return isNiBlock( item, QLatin1String(testType) );
}
inline bool NifModel::isNiBlock( const QModelIndex & index ) const
{
	return isNiBlock( getItem(index) );
}
inline bool NifModel::isNiBlock( const QModelIndex & index, const QString & testType ) const
{
	return isNiBlock( getItem(index), testType );
}
inline bool NifModel::isNiBlock( const QModelIndex & index, const QLatin1String & testType ) const
{
	return isNiBlock( getItem(index), testType );
}
inline bool NifModel::isNiBlock( const QModelIndex & index, const char * testType ) const
{
	return isNiBlock( getItem(index), QLatin1String(testType) );
}
inline bool NifModel::isNiBlock( const QModelIndex & index, const std::initializer_list<const char *> & testTypes ) const
{
	return isNiBlock( getItem(index), testTypes );
}
inline bool NifModel::isNiBlock( const QModelIndex & index, const QStringList & testTypes ) const
{
	return isNiBlock( getItem(index), testTypes );
}


// Block inheritance

inline bool NifModel::inherits( const QString & blockName, const char * ancestor ) const
{
	return inherits( blockName, QLatin1String(ancestor) );
}
inline bool NifModel::blockInherits( const NifItem * item, const char * ancestor ) const
{
	return blockInherits( item, QLatin1String(ancestor) );
}
inline bool NifModel::blockInherits( const QModelIndex & index, const QString & ancestor ) const
{
	return blockInherits( getItem(index), ancestor );
}
inline bool NifModel::blockInherits( const QModelIndex & index, const QLatin1String & ancestor ) const
{
	return blockInherits( getItem(index), ancestor );
}
inline bool NifModel::blockInherits( const QModelIndex & index, const char * ancestor ) const
{
	return blockInherits( getItem(index), QLatin1String(ancestor) );
}
inline bool NifModel::blockInherits( const QModelIndex & index, const std::initializer_list<const char *> & ancestors ) const
{
	return blockInherits( getItem(index), ancestors );
}
inline bool NifModel::blockInherits( const QModelIndex & index, const QStringList & ancestors ) const
{
	return blockInherits( getItem(index), ancestors );
}


// Item value getters

template <typename T> inline T NifModel::get( const NifItem * item ) const
{
	return BaseModel::get<T>( item );
}
template <> inline QString NifModel::get( const NifItem * item ) const
{
	return resolveString( item );
}
template <typename T> inline T NifModel::get( const NifItem * itemParent, int itemIndex ) const
{
	return get<T>( getItem(itemParent, itemIndex) );
}
template <typename T> inline T NifModel::get( const NifItem * itemParent, const QString & itemName ) const
{
	return get<T>( getItem(itemParent, itemName) );
}
template <typename T> inline T NifModel::get( const NifItem * itemParent, const QLatin1String & itemName ) const
{
	return get<T>( getItem(itemParent, itemName) );
}
template <typename T> inline T NifModel::get( const NifItem * itemParent, const char * itemName ) const
{
	return get<T>( getItem(itemParent, QLatin1String(itemName)) );
}
template <typename T> inline T NifModel::get( const QModelIndex & index ) const
{
	return get<T>( getItem(index) );
}
template <typename T> inline T NifModel::get( const QModelIndex & itemParent, int itemIndex ) const
{
	return get<T>( getItem(itemParent, itemIndex) );
}
template <typename T> inline T NifModel::get( const QModelIndex & itemParent, const QString & itemName ) const
{
	return get<T>( getItem(itemParent, itemName) );
}
template <typename T> inline T NifModel::get( const QModelIndex & itemParent, const QLatin1String & itemName ) const
{
	return get<T>( getItem(itemParent, itemName) );
}
template <typename T> inline T NifModel::get( const QModelIndex & itemParent, const char * itemName ) const
{
	return get<T>( getItem(itemParent, QLatin1String(itemName)) );
}


// Item value setters

template <typename T> inline bool NifModel::set( NifItem * item, const T & val )
{
	return BaseModel::set<T>( item, val );
}
template <> inline bool NifModel::set( NifItem * item, const QString & val )
{
	return assignString( item, val );
}
template <typename T> inline bool NifModel::set( const NifItem * itemParent, int itemIndex, const T & val )
{
	return set<T>( getItem(itemParent, itemIndex, true), val );
}
template <typename T> inline bool NifModel::set( const NifItem * itemParent, const QString & itemName, const T & val )
{
	return set<T>( getItem(itemParent, itemName, true), val );
}
template <typename T> inline bool NifModel::set( const NifItem * itemParent, const QLatin1String & itemName, const T & val )
{
	return set<T>( getItem(itemParent, itemName, true), val );
}
template <typename T> inline bool NifModel::set( const NifItem * itemParent, const char * itemName, const T & val )
{
	return set<T>( getItem(itemParent, QLatin1String(itemName), true), val );
}
template <typename T> inline bool NifModel::set( const QModelIndex & index, const T & val )
{
	return set<T>( getItem(index), val );
}
template <typename T> inline bool NifModel::set( const QModelIndex & itemParent, int itemIndex, const T & val )
{
	return set<T>( getItem(itemParent, itemIndex, true), val );
}
template <typename T> inline bool NifModel::set( const QModelIndex & itemParent, const QString & itemName, const T & val )
{
	return set<T>( getItem(itemParent, itemName, true), val );
}
template <typename T> inline bool NifModel::set( const QModelIndex & itemParent, const QLatin1String & itemName, const T & val )
{
	return set<T>( getItem(itemParent, itemName, true), val );
}
template <typename T> inline bool NifModel::set( const QModelIndex & itemParent, const char * itemName, const T & val )
{
	return set<T>( getItem(itemParent, QLatin1String(itemName), true), val );
}


// String resolving ("get ex")

inline QString NifModel::resolveString( const NifItem * itemParent, int itemIndex ) const
{
	return resolveString( getItem(itemParent, itemIndex) );
}
inline QString NifModel::resolveString( const NifItem * itemParent, const QString & itemName ) const
{
	return resolveString( getItem(itemParent, itemName) );
}
inline QString NifModel::resolveString( const NifItem * itemParent, const QLatin1String & itemName ) const
{
	return resolveString( getItem(itemParent, itemName) );
}
inline QString NifModel::resolveString( const NifItem * itemParent, const char * itemName ) const
{
	return resolveString( getItem(itemParent, QLatin1String(itemName)) );
}
inline QString NifModel::resolveString( const QModelIndex & index ) const
{
	return resolveString( getItem(index) );
}
inline QString NifModel::resolveString( const QModelIndex & itemParent, int itemIndex ) const
{
	return resolveString( getItem(itemParent, itemIndex) );
}
inline QString NifModel::resolveString( const QModelIndex & itemParent, const QString & itemName ) const
{
	return resolveString( getItem(itemParent, itemName) );
}
inline QString NifModel::resolveString( const QModelIndex & itemParent, const QLatin1String & itemName ) const
{
	return resolveString( getItem(itemParent, itemName) );
}
inline QString NifModel::resolveString( const QModelIndex & itemParent, const char * itemName ) const
{
	return resolveString( getItem(itemParent, QLatin1String(itemName)) );
}


// String assigning ("set ex")

inline bool NifModel::assignString( const NifItem * itemParent, int itemIndex, const QString & string, bool replace )
{
	return assignString( getItem(itemParent, itemIndex, true), string, replace );
}
inline bool NifModel::assignString( const NifItem * itemParent, const QString & itemName, const QString & string, bool replace )
{
	return assignString( getItem(itemParent, itemName, true), string, replace );
}
inline bool NifModel::assignString( const NifItem * itemParent, const QLatin1String & itemName, const QString & string, bool replace )
{
	return assignString( getItem(itemParent, itemName, true), string, replace );
}
inline bool NifModel::assignString( const NifItem * itemParent, const char * itemName, const QString & string, bool replace )
{
	return assignString( getItem(itemParent, QLatin1String(itemName), true), string, replace );
}
inline bool NifModel::assignString( const QModelIndex & index, const QString & string, bool replace )
{
	return assignString( getItem(index), string, replace );
}
inline bool NifModel::assignString( const QModelIndex & itemParent, int itemIndex, const QString & string, bool replace )
{
	return assignString( getItem(itemParent, itemIndex, true), string, replace );
}
inline bool NifModel::assignString( const QModelIndex & itemParent, const QString & itemName, const QString & string, bool replace )
{
	return assignString( getItem(itemParent, itemName, true), string, replace );
}
inline bool NifModel::assignString( const QModelIndex & itemParent, const QLatin1String & itemName, const QString & string, bool replace )
{
	return assignString( getItem(itemParent, itemName, true), string, replace );
}
inline bool NifModel::assignString( const QModelIndex & itemParent, const char * itemName, const QString & string, bool replace )
{
	return assignString( getItem(itemParent, QLatin1String(itemName), true), string, replace );
}


// Link getters

inline qint32 NifModel::getLink( const NifItem * item ) const
{
	return item ? item->getLinkValue() : -1;
}
inline qint32 NifModel::getLink( const NifItem * itemParent, int itemIndex ) const
{
	return getLink( getItem(itemParent, itemIndex) );
}
inline qint32 NifModel::getLink( const NifItem * itemParent, const QString & itemName ) const
{
	return getLink( getItem(itemParent, itemName) );
}
inline qint32 NifModel::getLink( const NifItem * itemParent, const QLatin1String & itemName ) const
{
	return getLink( getItem(itemParent, itemName) );
}
inline qint32 NifModel::getLink( const NifItem * itemParent, const char * itemName ) const
{
	return getLink( getItem(itemParent, QLatin1String(itemName)) );
}
inline qint32 NifModel::getLink( const QModelIndex & index ) const
{
	return getLink( getItem(index) );
}
inline qint32 NifModel::getLink( const QModelIndex & itemParent, int itemIndex ) const
{
	return getLink( getItem(itemParent, itemIndex) );
}
inline qint32 NifModel::getLink( const QModelIndex & itemParent, const QString & itemName ) const
{
	return getLink( getItem(itemParent, itemName) );
}
inline qint32 NifModel::getLink( const QModelIndex & itemParent, const QLatin1String & itemName ) const
{
	return getLink( getItem(itemParent, itemName) );
}
inline qint32 NifModel::getLink( const QModelIndex & itemParent, const char * itemName ) const
{
	return getLink( getItem(itemParent, QLatin1String(itemName)) );
}


// Link setters

inline bool NifModel::setLink( const NifItem * itemParent, int itemIndex, qint32 link )
{
	return setLink( getItem(itemParent, itemIndex, true), link );
}
inline bool NifModel::setLink( const NifItem * itemParent, const QString & itemName, qint32 link )
{
	return setLink( getItem(itemParent, itemName, true), link );
}
inline bool NifModel::setLink( const NifItem * itemParent, const QLatin1String & itemName, qint32 link )
{
	return setLink( getItem(itemParent, itemName, true), link );
}
inline bool NifModel::setLink( const NifItem * itemParent, const char * itemName, qint32 link )
{
	return setLink( getItem(itemParent, QLatin1String(itemName), true), link );
}
inline bool NifModel::setLink( const QModelIndex & index, qint32 link )
{
	return setLink( getItem(index), link );
}
inline bool NifModel::setLink( const QModelIndex & itemParent, int itemIndex, qint32 link )
{
	return setLink( getItem(itemParent, itemIndex, true), link );
}
inline bool NifModel::setLink( const QModelIndex & itemParent, const QString & itemName, qint32 link )
{
	return setLink( getItem(itemParent, itemName, true), link );
}
inline bool NifModel::setLink( const QModelIndex & itemParent, const QLatin1String & itemName, qint32 link )
{
	return setLink( getItem(itemParent, itemName, true), link );
}
inline bool NifModel::setLink( const QModelIndex & itemParent, const char * itemName, qint32 link )
{
	return setLink( getItem(itemParent, QLatin1String(itemName), true), link );
}


// Link array getters

inline QVector<qint32> NifModel::getLinkArray( const NifItem * arrayParent, int arrayIndex ) const
{
	return getLinkArray( getItem(arrayParent, arrayIndex) );
}
inline QVector<qint32> NifModel::getLinkArray( const NifItem * arrayParent, const QString & arrayName ) const
{
	return getLinkArray( getItem(arrayParent, arrayName) );
}
inline QVector<qint32> NifModel::getLinkArray( const NifItem * arrayParent, const QLatin1String & arrayName ) const
{
	return getLinkArray( getItem(arrayParent, arrayName) );
}
inline QVector<qint32> NifModel::getLinkArray( const NifItem * arrayParent, const char * arrayName ) const
{
	return getLinkArray( getItem(arrayParent, QLatin1String(arrayName)) );
}
inline QVector<qint32> NifModel::getLinkArray( const QModelIndex & iArray ) const
{
	return getLinkArray( getItem(iArray) );
}
inline QVector<qint32> NifModel::getLinkArray( const QModelIndex & arrayParent, int arrayIndex ) const
{
	return getLinkArray( getItem(arrayParent, arrayIndex) );
}
inline QVector<qint32> NifModel::getLinkArray( const QModelIndex & arrayParent, const QString & arrayName ) const
{
	return getLinkArray( getItem(arrayParent, arrayName) );
}
inline QVector<qint32> NifModel::getLinkArray( const QModelIndex & arrayParent, const QLatin1String & arrayName ) const
{
	return getLinkArray( getItem(arrayParent, arrayName) );
}
inline QVector<qint32> NifModel::getLinkArray( const QModelIndex & arrayParent, const char * arrayName ) const
{
	return getLinkArray( getItem(arrayParent, QLatin1String(arrayName)) );
}


// Link array setters

inline bool NifModel::setLinkArray( const NifItem * arrayParent, int arrayIndex, const QVector<qint32> & links )
{
	return setLinkArray( getItem(arrayParent, arrayIndex, true), links );
}
inline bool NifModel::setLinkArray( const NifItem * arrayParent, const QString & arrayName, const QVector<qint32> & links )
{
	return setLinkArray( getItem(arrayParent, arrayName, true), links );
}
inline bool NifModel::setLinkArray( const NifItem * arrayParent, const QLatin1String & arrayName, const QVector<qint32> & links )
{
	return setLinkArray( getItem(arrayParent, arrayName, true), links );
}
inline bool NifModel::setLinkArray( const NifItem * arrayParent, const char * arrayName, const QVector<qint32> & links )
{
	return setLinkArray( getItem(arrayParent, QLatin1String(arrayName), true), links );
}
inline bool NifModel::setLinkArray( const QModelIndex & iArray, const QVector<qint32> & links )
{
	return setLinkArray( getItem(iArray), links );
}
inline bool NifModel::setLinkArray( const QModelIndex & arrayParent, int arrayIndex, const QVector<qint32> & links )
{
	return setLinkArray( getItem(arrayParent, arrayIndex, true), links );
}
inline bool NifModel::setLinkArray( const QModelIndex & arrayParent, const QString & arrayName, const QVector<qint32> & links )
{
	return setLinkArray( getItem(arrayParent, arrayName, true), links );
}
inline bool NifModel::setLinkArray( const QModelIndex & arrayParent, const QLatin1String & arrayName, const QVector<qint32> & links )
{
	return setLinkArray( getItem(arrayParent, arrayName, true), links );
}
inline bool NifModel::setLinkArray( const QModelIndex & arrayParent, const char * arrayName, const QVector<qint32> & links )
{
	return setLinkArray( getItem(arrayParent, QLatin1String(arrayName), true), links );
}


// NifModel -> NifFieldTemplate methods

inline NifFieldConst NifModel::field( const QModelIndex & index, bool reportErrors ) const
{
	auto item = getItem( index, reportErrors );
	return NifFieldConst( evalCondition(item) ? item : nullptr );
}
inline NifField NifModel::field( const QModelIndex & index, bool reportErrors )
{
	auto item = getItem( index, reportErrors );
	return NifField( evalCondition(item) ? item : nullptr );
}

inline NifFieldConst NifModel::block( quint32 link ) const
{
	return NifFieldConst( getBlockItem(link) );
}
inline NifField NifModel::block( quint32 link )
{
	return NifField( getBlockItem(link) );
}

inline NifFieldConst NifModel::block( const QModelIndex & index, bool reportErrors ) const
{
	return field( index, reportErrors ).block();
}
inline NifField NifModel::block( const QModelIndex & index, bool reportErrors )
{
	return field( index, reportErrors ).block();
}

// TODO: Move to nifmodel.cpp
inline const NifItem * NifModel::findBlockItemByName( const QString & blockName ) const
{
	for ( int i = firstBlockRow(), iLast = lastBlockRow(); i <= iLast; i++ ) {
		auto block = root->child(i);
		auto nameItem = getItem( block, "Name", false );
		if ( nameItem && get<QString>(nameItem) == blockName )
			return block;
	}
	return nullptr;
}
// TODO: Move to nifmodel.cpp
inline const NifItem * NifModel::findBlockItemByName( const QLatin1String & blockName ) const
{
	for ( int i = firstBlockRow(), iLast = lastBlockRow(); i <= iLast; i++ ) {
		auto block = root->child(i);
		auto nameItem = getItem( block, "Name", false );
		if ( nameItem && get<QString>(nameItem) == blockName )
			return block;
	}
	return nullptr;
}

inline NifFieldConst NifModel::block( const QString & blockName ) const
{
	return NifFieldConst( findBlockItemByName(blockName) );
}
inline NifField NifModel::block( const QString & blockName )
{
	return NifField( const_cast<NifItem *>( findBlockItemByName(blockName) ) );
}
inline NifFieldConst NifModel::block( const QLatin1String & blockName ) const
{
	return NifFieldConst( findBlockItemByName(blockName) );
}
inline NifField NifModel::block( const QLatin1String & blockName )
{
	return NifField( const_cast<NifItem *>( findBlockItemByName(blockName) ) );
}
inline NifFieldConst NifModel::block( const char * blockName ) const
{
	return block( QLatin1String(blockName) );
}
inline NifField NifModel::block( const char * blockName )
{
	return block( QLatin1String(blockName) );
}

inline NifFieldConst NifModel::header() const
{
	return NifFieldConst( getHeaderItem() );
}
inline NifField NifModel::header()
{
	return NifField( getHeaderItem() );
}

inline NifFieldConst NifModel::footer() const
{
	return NifFieldConst( getFooterItem() );
}
inline NifField NifModel::footer()
{
	return NifField( getFooterItem() );
}

inline NifFieldIteratorSimple<const NifModel *, const NifItem *> NifModel::blockIter() const
{
	return NifFieldIteratorSimple<const NifModel *, const NifItem *>( root, firstBlockRow(), lastBlockRow() );
}

inline NifFieldIteratorSimple<NifModel *, NifItem *> NifModel::blockIter()
{
	return NifFieldIteratorSimple<NifModel *, NifItem *>( root, firstBlockRow(), lastBlockRow() );
}


// NifFieldTemplate - isBlockType() methods (generated)

template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::isBlockType( const QString & testType ) const
{
	return isBlock() && m_item->hasName(testType);
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::isBlockType( const QString & testType1, const QString & testType2 ) const
{
	return isBlock() && ( m_item->hasName(testType1) || m_item->hasName(testType2) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::isBlockType( const QString & testType1, const QString & testType2, const QString & testType3 ) const
{
	return isBlock() && ( m_item->hasName(testType1) || m_item->hasName(testType2) || m_item->hasName(testType3) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::isBlockType( const QString & testType1, const QString & testType2, const QString & testType3, const QString & testType4 ) const
{
	return isBlock() && ( m_item->hasName(testType1) || m_item->hasName(testType2) || m_item->hasName(testType3) || m_item->hasName(testType4) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::isBlockType( const QString & testType1, const QString & testType2, const QString & testType3, const QString & testType4, const QString & testType5 ) const
{
	return isBlock() && ( m_item->hasName(testType1) || m_item->hasName(testType2) || m_item->hasName(testType3) || m_item->hasName(testType4) || m_item->hasName(testType5) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::isBlockType( const QLatin1String & testType ) const
{
	return isBlock() && m_item->hasName(testType);
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::isBlockType( const QLatin1String & testType1, const QLatin1String & testType2 ) const
{
	return isBlock() && ( m_item->hasName(testType1) || m_item->hasName(testType2) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::isBlockType( const QLatin1String & testType1, const QLatin1String & testType2, const QLatin1String & testType3 ) const
{
	return isBlock() && ( m_item->hasName(testType1) || m_item->hasName(testType2) || m_item->hasName(testType3) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::isBlockType( const QLatin1String & testType1, const QLatin1String & testType2, const QLatin1String & testType3, const QLatin1String & testType4 ) const
{
	return isBlock() && ( m_item->hasName(testType1) || m_item->hasName(testType2) || m_item->hasName(testType3) || m_item->hasName(testType4) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::isBlockType( const QLatin1String & testType1, const QLatin1String & testType2, const QLatin1String & testType3, const QLatin1String & testType4, const QLatin1String & testType5 ) const
{
	return isBlock() && ( m_item->hasName(testType1) || m_item->hasName(testType2) || m_item->hasName(testType3) || m_item->hasName(testType4) || m_item->hasName(testType5) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::isBlockType( const char * testType ) const
{
	return isBlockType( QLatin1String(testType) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::isBlockType( const char * testType1, const char * testType2 ) const
{
	return isBlockType( QLatin1String(testType1), QLatin1String(testType2) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::isBlockType( const char * testType1, const char * testType2, const char * testType3 ) const
{
	return isBlockType( QLatin1String(testType1), QLatin1String(testType2), QLatin1String(testType3) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::isBlockType( const char * testType1, const char * testType2, const char * testType3, const char * testType4 ) const
{
	return isBlockType( QLatin1String(testType1), QLatin1String(testType2), QLatin1String(testType3), QLatin1String(testType4) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::isBlockType( const char * testType1, const char * testType2, const char * testType3, const char * testType4, const char * testType5 ) const
{
	return isBlockType( QLatin1String(testType1), QLatin1String(testType2), QLatin1String(testType3), QLatin1String(testType4), QLatin1String(testType5) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::isBlockType( const QStringList & testTypes ) const
{
	if ( isBlock() ) {
		for ( const QString & name : testTypes )
			if ( m_item->hasName(name) )
				return true;
	}
	return false;
}


// NifFieldTemplate - inherits() methods (generated)

template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::inherits( const QString & testAncestor ) const
{
	return isBlock() && _inherits( m_item, testAncestor );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::inherits( const QString & testAncestor1, const QString & testAncestor2 ) const
{
	return isBlock() && ( _inherits( m_item, testAncestor1 ) || _inherits( m_item, testAncestor2 ) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::inherits( const QString & testAncestor1, const QString & testAncestor2, const QString & testAncestor3 ) const
{
	return isBlock() && ( _inherits( m_item, testAncestor1 ) || _inherits( m_item, testAncestor2 ) || _inherits( m_item, testAncestor3 ) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::inherits( const QString & testAncestor1, const QString & testAncestor2, const QString & testAncestor3, const QString & testAncestor4 ) const
{
	return isBlock() && ( _inherits( m_item, testAncestor1 ) || _inherits( m_item, testAncestor2 ) || _inherits( m_item, testAncestor3 ) || _inherits( m_item, testAncestor4 ) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::inherits( const QString & testAncestor1, const QString & testAncestor2, const QString & testAncestor3, const QString & testAncestor4, const QString & testAncestor5 ) const
{
	return isBlock() && ( _inherits( m_item, testAncestor1 ) || _inherits( m_item, testAncestor2 ) || _inherits( m_item, testAncestor3 ) || _inherits( m_item, testAncestor4 ) || _inherits( m_item, testAncestor5 ) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::inherits( const QLatin1String & testAncestor ) const
{
	return isBlock() && _inherits( m_item, testAncestor );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::inherits( const QLatin1String & testAncestor1, const QLatin1String & testAncestor2 ) const
{
	return isBlock() && ( _inherits( m_item, testAncestor1 ) || _inherits( m_item, testAncestor2 ) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::inherits( const QLatin1String & testAncestor1, const QLatin1String & testAncestor2, const QLatin1String & testAncestor3 ) const
{
	return isBlock() && ( _inherits( m_item, testAncestor1 ) || _inherits( m_item, testAncestor2 ) || _inherits( m_item, testAncestor3 ) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::inherits( const QLatin1String & testAncestor1, const QLatin1String & testAncestor2, const QLatin1String & testAncestor3, const QLatin1String & testAncestor4 ) const
{
	return isBlock() && ( _inherits( m_item, testAncestor1 ) || _inherits( m_item, testAncestor2 ) || _inherits( m_item, testAncestor3 ) || _inherits( m_item, testAncestor4 ) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::inherits( const QLatin1String & testAncestor1, const QLatin1String & testAncestor2, const QLatin1String & testAncestor3, const QLatin1String & testAncestor4, const QLatin1String & testAncestor5 ) const
{
	return isBlock() && ( _inherits( m_item, testAncestor1 ) || _inherits( m_item, testAncestor2 ) || _inherits( m_item, testAncestor3 ) || _inherits( m_item, testAncestor4 ) || _inherits( m_item, testAncestor5 ) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::inherits( const char * testAncestor ) const
{
	return inherits( QLatin1String(testAncestor) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::inherits( const char * testAncestor1, const char * testAncestor2 ) const
{
	return inherits( QLatin1String(testAncestor1), QLatin1String(testAncestor2) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::inherits( const char * testAncestor1, const char * testAncestor2, const char * testAncestor3 ) const
{
	return inherits( QLatin1String(testAncestor1), QLatin1String(testAncestor2), QLatin1String(testAncestor3) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::inherits( const char * testAncestor1, const char * testAncestor2, const char * testAncestor3, const char * testAncestor4 ) const
{
	return inherits( QLatin1String(testAncestor1), QLatin1String(testAncestor2), QLatin1String(testAncestor3), QLatin1String(testAncestor4) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::inherits( const char * testAncestor1, const char * testAncestor2, const char * testAncestor3, const char * testAncestor4, const char * testAncestor5 ) const
{
	return inherits( QLatin1String(testAncestor1), QLatin1String(testAncestor2), QLatin1String(testAncestor3), QLatin1String(testAncestor4), QLatin1String(testAncestor5) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::inherits( const QStringList & testAncestors ) const
{
	return isBlock() && _inherits( m_item, testAncestors );
}


// NifFieldTemplate - block() methods (generated)

template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::block( const QString & testAncestor ) const
{
	if ( m_item ) {
		auto block = _model()->getBlockItem( m_item );    
		if ( block && _inherits( block, testAncestor ) )
			return NifFieldTemplate<ModelPtr, ItemPtr>( block );
	}
	return NifFieldTemplate<ModelPtr, ItemPtr>();
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::block( const QString & testAncestor1, const QString & testAncestor2 ) const
{
	if ( m_item ) {
		auto block = _model()->getBlockItem( m_item );    
		if ( block && ( _inherits( block, testAncestor1 ) || _inherits( block, testAncestor2 ) ) )
			return NifFieldTemplate<ModelPtr, ItemPtr>( block );
	}
	return NifFieldTemplate<ModelPtr, ItemPtr>();
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::block( const QString & testAncestor1, const QString & testAncestor2, const QString & testAncestor3 ) const
{
	if ( m_item ) {
		auto block = _model()->getBlockItem( m_item );    
		if ( block && ( _inherits( block, testAncestor1 ) || _inherits( block, testAncestor2 ) || _inherits( block, testAncestor3 ) ) )
			return NifFieldTemplate<ModelPtr, ItemPtr>( block );
	}
	return NifFieldTemplate<ModelPtr, ItemPtr>();
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::block( const QString & testAncestor1, const QString & testAncestor2, const QString & testAncestor3, const QString & testAncestor4 ) const
{
	if ( m_item ) {
		auto block = _model()->getBlockItem( m_item );    
		if ( block && ( _inherits( block, testAncestor1 ) || _inherits( block, testAncestor2 ) || _inherits( block, testAncestor3 ) || _inherits( block, testAncestor4 ) ) )
			return NifFieldTemplate<ModelPtr, ItemPtr>( block );
	}
	return NifFieldTemplate<ModelPtr, ItemPtr>();
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::block( const QString & testAncestor1, const QString & testAncestor2, const QString & testAncestor3, const QString & testAncestor4, const QString & testAncestor5 ) const
{
	if ( m_item ) {
		auto block = _model()->getBlockItem( m_item );    
		if ( block && ( _inherits( block, testAncestor1 ) || _inherits( block, testAncestor2 ) || _inherits( block, testAncestor3 ) || _inherits( block, testAncestor4 ) || _inherits( block, testAncestor5 ) ) )
			return NifFieldTemplate<ModelPtr, ItemPtr>( block );
	}
	return NifFieldTemplate<ModelPtr, ItemPtr>();
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::block( const QLatin1String & testAncestor ) const
{
	if ( m_item ) {
		auto block = _model()->getBlockItem( m_item );    
		if ( block && _inherits( block, testAncestor ) )
			return NifFieldTemplate<ModelPtr, ItemPtr>( block );
	}
	return NifFieldTemplate<ModelPtr, ItemPtr>();
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::block( const QLatin1String & testAncestor1, const QLatin1String & testAncestor2 ) const
{
	if ( m_item ) {
		auto block = _model()->getBlockItem( m_item );    
		if ( block && ( _inherits( block, testAncestor1 ) || _inherits( block, testAncestor2 ) ) )
			return NifFieldTemplate<ModelPtr, ItemPtr>( block );
	}
	return NifFieldTemplate<ModelPtr, ItemPtr>();
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::block( const QLatin1String & testAncestor1, const QLatin1String & testAncestor2, const QLatin1String & testAncestor3 ) const
{
	if ( m_item ) {
		auto block = _model()->getBlockItem( m_item );    
		if ( block && ( _inherits( block, testAncestor1 ) || _inherits( block, testAncestor2 ) || _inherits( block, testAncestor3 ) ) )
			return NifFieldTemplate<ModelPtr, ItemPtr>( block );
	}
	return NifFieldTemplate<ModelPtr, ItemPtr>();
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::block( const QLatin1String & testAncestor1, const QLatin1String & testAncestor2, const QLatin1String & testAncestor3, const QLatin1String & testAncestor4 ) const
{
	if ( m_item ) {
		auto block = _model()->getBlockItem( m_item );    
		if ( block && ( _inherits( block, testAncestor1 ) || _inherits( block, testAncestor2 ) || _inherits( block, testAncestor3 ) || _inherits( block, testAncestor4 ) ) )
			return NifFieldTemplate<ModelPtr, ItemPtr>( block );
	}
	return NifFieldTemplate<ModelPtr, ItemPtr>();
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::block( const QLatin1String & testAncestor1, const QLatin1String & testAncestor2, const QLatin1String & testAncestor3, const QLatin1String & testAncestor4, const QLatin1String & testAncestor5 ) const
{
	if ( m_item ) {
		auto block = _model()->getBlockItem( m_item );    
		if ( block && ( _inherits( block, testAncestor1 ) || _inherits( block, testAncestor2 ) || _inherits( block, testAncestor3 ) || _inherits( block, testAncestor4 ) || _inherits( block, testAncestor5 ) ) )
			return NifFieldTemplate<ModelPtr, ItemPtr>( block );
	}
	return NifFieldTemplate<ModelPtr, ItemPtr>();
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::block( const char * testAncestor ) const
{
	return block( QLatin1String(testAncestor) );
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::block( const char * testAncestor1, const char * testAncestor2 ) const
{
	return block( QLatin1String(testAncestor1), QLatin1String(testAncestor2) );
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::block( const char * testAncestor1, const char * testAncestor2, const char * testAncestor3 ) const
{
	return block( QLatin1String(testAncestor1), QLatin1String(testAncestor2), QLatin1String(testAncestor3) );
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::block( const char * testAncestor1, const char * testAncestor2, const char * testAncestor3, const char * testAncestor4 ) const
{
	return block( QLatin1String(testAncestor1), QLatin1String(testAncestor2), QLatin1String(testAncestor3), QLatin1String(testAncestor4) );
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::block( const char * testAncestor1, const char * testAncestor2, const char * testAncestor3, const char * testAncestor4, const char * testAncestor5 ) const
{
	return block( QLatin1String(testAncestor1), QLatin1String(testAncestor2), QLatin1String(testAncestor3), QLatin1String(testAncestor4), QLatin1String(testAncestor5) );
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::block( const QStringList & testAncestors ) const
{
	if ( m_item ) {
		auto block = _model()->getBlockItem( m_item );    
		if ( block && _inherits( block, testAncestors ) )
			return NifFieldTemplate<ModelPtr, ItemPtr>( block );
	}
	return NifFieldTemplate<ModelPtr, ItemPtr>();
}


// NifFieldTemplate - hasName() methods (generated)

template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasName( const QString & testName ) const
{
	return m_item && m_item->hasName(testName);
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasName( const QString & testName1, const QString & testName2 ) const
{
	return m_item && ( m_item->hasName(testName1) || m_item->hasName(testName2) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasName( const QString & testName1, const QString & testName2, const QString & testName3 ) const
{
	return m_item && ( m_item->hasName(testName1) || m_item->hasName(testName2) || m_item->hasName(testName3) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasName( const QString & testName1, const QString & testName2, const QString & testName3, const QString & testName4 ) const
{
	return m_item && ( m_item->hasName(testName1) || m_item->hasName(testName2) || m_item->hasName(testName3) || m_item->hasName(testName4) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasName( const QString & testName1, const QString & testName2, const QString & testName3, const QString & testName4, const QString & testName5 ) const
{
	return m_item && ( m_item->hasName(testName1) || m_item->hasName(testName2) || m_item->hasName(testName3) || m_item->hasName(testName4) || m_item->hasName(testName5) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasName( const QLatin1String & testName ) const
{
	return m_item && m_item->hasName(testName);
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasName( const QLatin1String & testName1, const QLatin1String & testName2 ) const
{
	return m_item && ( m_item->hasName(testName1) || m_item->hasName(testName2) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasName( const QLatin1String & testName1, const QLatin1String & testName2, const QLatin1String & testName3 ) const
{
	return m_item && ( m_item->hasName(testName1) || m_item->hasName(testName2) || m_item->hasName(testName3) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasName( const QLatin1String & testName1, const QLatin1String & testName2, const QLatin1String & testName3, const QLatin1String & testName4 ) const
{
	return m_item && ( m_item->hasName(testName1) || m_item->hasName(testName2) || m_item->hasName(testName3) || m_item->hasName(testName4) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasName( const QLatin1String & testName1, const QLatin1String & testName2, const QLatin1String & testName3, const QLatin1String & testName4, const QLatin1String & testName5 ) const
{
	return m_item && ( m_item->hasName(testName1) || m_item->hasName(testName2) || m_item->hasName(testName3) || m_item->hasName(testName4) || m_item->hasName(testName5) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasName( const char * testName ) const
{
	return hasName( QLatin1String(testName) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasName( const char * testName1, const char * testName2 ) const
{
	return hasName( QLatin1String(testName1), QLatin1String(testName2) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasName( const char * testName1, const char * testName2, const char * testName3 ) const
{
	return hasName( QLatin1String(testName1), QLatin1String(testName2), QLatin1String(testName3) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasName( const char * testName1, const char * testName2, const char * testName3, const char * testName4 ) const
{
	return hasName( QLatin1String(testName1), QLatin1String(testName2), QLatin1String(testName3), QLatin1String(testName4) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasName( const char * testName1, const char * testName2, const char * testName3, const char * testName4, const char * testName5 ) const
{
	return hasName( QLatin1String(testName1), QLatin1String(testName2), QLatin1String(testName3), QLatin1String(testName4), QLatin1String(testName5) );
}


// NifFieldTemplate - hasStrType() methods (generated)

template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasStrType( const QString & testType ) const
{
	return m_item && m_item->hasStrType(testType);
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasStrType( const QString & testType1, const QString & testType2 ) const
{
	return m_item && ( m_item->hasStrType(testType1) || m_item->hasStrType(testType2) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasStrType( const QString & testType1, const QString & testType2, const QString & testType3 ) const
{
	return m_item && ( m_item->hasStrType(testType1) || m_item->hasStrType(testType2) || m_item->hasStrType(testType3) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasStrType( const QString & testType1, const QString & testType2, const QString & testType3, const QString & testType4 ) const
{
	return m_item && ( m_item->hasStrType(testType1) || m_item->hasStrType(testType2) || m_item->hasStrType(testType3) || m_item->hasStrType(testType4) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasStrType( const QString & testType1, const QString & testType2, const QString & testType3, const QString & testType4, const QString & testType5 ) const
{
	return m_item && ( m_item->hasStrType(testType1) || m_item->hasStrType(testType2) || m_item->hasStrType(testType3) || m_item->hasStrType(testType4) || m_item->hasStrType(testType5) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasStrType( const QLatin1String & testType ) const
{
	return m_item && m_item->hasStrType(testType);
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasStrType( const QLatin1String & testType1, const QLatin1String & testType2 ) const
{
	return m_item && ( m_item->hasStrType(testType1) || m_item->hasStrType(testType2) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasStrType( const QLatin1String & testType1, const QLatin1String & testType2, const QLatin1String & testType3 ) const
{
	return m_item && ( m_item->hasStrType(testType1) || m_item->hasStrType(testType2) || m_item->hasStrType(testType3) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasStrType( const QLatin1String & testType1, const QLatin1String & testType2, const QLatin1String & testType3, const QLatin1String & testType4 ) const
{
	return m_item && ( m_item->hasStrType(testType1) || m_item->hasStrType(testType2) || m_item->hasStrType(testType3) || m_item->hasStrType(testType4) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasStrType( const QLatin1String & testType1, const QLatin1String & testType2, const QLatin1String & testType3, const QLatin1String & testType4, const QLatin1String & testType5 ) const
{
	return m_item && ( m_item->hasStrType(testType1) || m_item->hasStrType(testType2) || m_item->hasStrType(testType3) || m_item->hasStrType(testType4) || m_item->hasStrType(testType5) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasStrType( const char * testType ) const
{
	return hasStrType( QLatin1String(testType) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasStrType( const char * testType1, const char * testType2 ) const
{
	return hasStrType( QLatin1String(testType1), QLatin1String(testType2) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasStrType( const char * testType1, const char * testType2, const char * testType3 ) const
{
	return hasStrType( QLatin1String(testType1), QLatin1String(testType2), QLatin1String(testType3) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasStrType( const char * testType1, const char * testType2, const char * testType3, const char * testType4 ) const
{
	return hasStrType( QLatin1String(testType1), QLatin1String(testType2), QLatin1String(testType3), QLatin1String(testType4) );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasStrType( const char * testType1, const char * testType2, const char * testType3, const char * testType4, const char * testType5 ) const
{
	return hasStrType( QLatin1String(testType1), QLatin1String(testType2), QLatin1String(testType3), QLatin1String(testType4), QLatin1String(testType5) );
}


// NifFieldTemplate - hasValType() methods (generated)

template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasValType( NifValue::Type testType ) const
{
	return m_item && m_item->valueType() == testType;
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasValType( NifValue::Type testType1, NifValue::Type testType2 ) const
{
	return m_item && ( m_item->valueType() == testType1 || m_item->valueType() == testType2 );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasValType( NifValue::Type testType1, NifValue::Type testType2, NifValue::Type testType3 ) const
{
	return m_item && ( m_item->valueType() == testType1 || m_item->valueType() == testType2 || m_item->valueType() == testType3 );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasValType( NifValue::Type testType1, NifValue::Type testType2, NifValue::Type testType3, NifValue::Type testType4 ) const
{
	return m_item && ( m_item->valueType() == testType1 || m_item->valueType() == testType2 || m_item->valueType() == testType3 || m_item->valueType() == testType4 );
}
template<typename ModelPtr, typename ItemPtr>
inline bool NifFieldTemplate<ModelPtr, ItemPtr>::hasValType( NifValue::Type testType1, NifValue::Type testType2, NifValue::Type testType3, NifValue::Type testType4, NifValue::Type testType5 ) const
{
	return m_item && ( m_item->valueType() == testType1 || m_item->valueType() == testType2 || m_item->valueType() == testType3 || m_item->valueType() == testType4 || m_item->valueType() == testType5 );
}


// NifFieldTemplate - getLinkBlock() methods (generated)

template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::linkBlock( const QString & testAncestor ) const
{
	if ( m_item ) {
		auto link = m_item->getLinkValue();
		auto block = _model()->getBlockItem( link );
		if ( block && _inherits( block, testAncestor ) )
			return NifFieldTemplate<ModelPtr, ItemPtr>( block );
	}
	return NifFieldTemplate<ModelPtr, ItemPtr>();
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::linkBlock( const QString & testAncestor1, const QString & testAncestor2 ) const
{
	if ( m_item ) {
		auto link = m_item->getLinkValue();
		auto block = _model()->getBlockItem( link );
		if ( block && ( _inherits( block, testAncestor1 ) || _inherits( block, testAncestor2 ) ) )
			return NifFieldTemplate<ModelPtr, ItemPtr>( block );
	}
	return NifFieldTemplate<ModelPtr, ItemPtr>();
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::linkBlock( const QString & testAncestor1, const QString & testAncestor2, const QString & testAncestor3 ) const
{
	if ( m_item ) {
		auto link = m_item->getLinkValue();
		auto block = _model()->getBlockItem( link );
		if ( block && ( _inherits( block, testAncestor1 ) || _inherits( block, testAncestor2 ) || _inherits( block, testAncestor3 ) ) )
			return NifFieldTemplate<ModelPtr, ItemPtr>( block );
	}
	return NifFieldTemplate<ModelPtr, ItemPtr>();
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::linkBlock( const QString & testAncestor1, const QString & testAncestor2, const QString & testAncestor3, const QString & testAncestor4 ) const
{
	if ( m_item ) {
		auto link = m_item->getLinkValue();
		auto block = _model()->getBlockItem( link );
		if ( block && ( _inherits( block, testAncestor1 ) || _inherits( block, testAncestor2 ) || _inherits( block, testAncestor3 ) || _inherits( block, testAncestor4 ) ) )
			return NifFieldTemplate<ModelPtr, ItemPtr>( block );
	}
	return NifFieldTemplate<ModelPtr, ItemPtr>();
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::linkBlock( const QString & testAncestor1, const QString & testAncestor2, const QString & testAncestor3, const QString & testAncestor4, const QString & testAncestor5 ) const
{
	if ( m_item ) {
		auto link = m_item->getLinkValue();
		auto block = _model()->getBlockItem( link );
		if ( block && ( _inherits( block, testAncestor1 ) || _inherits( block, testAncestor2 ) || _inherits( block, testAncestor3 ) || _inherits( block, testAncestor4 ) || _inherits( block, testAncestor5 ) ) )
			return NifFieldTemplate<ModelPtr, ItemPtr>( block );
	}
	return NifFieldTemplate<ModelPtr, ItemPtr>();
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::linkBlock( const QLatin1String & testAncestor ) const
{
	if ( m_item ) {
		auto link = m_item->getLinkValue();
		auto block = _model()->getBlockItem( link );
		if ( block && _inherits( block, testAncestor ) )
			return NifFieldTemplate<ModelPtr, ItemPtr>( block );
	}
	return NifFieldTemplate<ModelPtr, ItemPtr>();
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::linkBlock( const QLatin1String & testAncestor1, const QLatin1String & testAncestor2 ) const
{
	if ( m_item ) {
		auto link = m_item->getLinkValue();
		auto block = _model()->getBlockItem( link );
		if ( block && ( _inherits( block, testAncestor1 ) || _inherits( block, testAncestor2 ) ) )
			return NifFieldTemplate<ModelPtr, ItemPtr>( block );
	}
	return NifFieldTemplate<ModelPtr, ItemPtr>();
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::linkBlock( const QLatin1String & testAncestor1, const QLatin1String & testAncestor2, const QLatin1String & testAncestor3 ) const
{
	if ( m_item ) {
		auto link = m_item->getLinkValue();
		auto block = _model()->getBlockItem( link );
		if ( block && ( _inherits( block, testAncestor1 ) || _inherits( block, testAncestor2 ) || _inherits( block, testAncestor3 ) ) )
			return NifFieldTemplate<ModelPtr, ItemPtr>( block );
	}
	return NifFieldTemplate<ModelPtr, ItemPtr>();
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::linkBlock( const QLatin1String & testAncestor1, const QLatin1String & testAncestor2, const QLatin1String & testAncestor3, const QLatin1String & testAncestor4 ) const
{
	if ( m_item ) {
		auto link = m_item->getLinkValue();
		auto block = _model()->getBlockItem( link );
		if ( block && ( _inherits( block, testAncestor1 ) || _inherits( block, testAncestor2 ) || _inherits( block, testAncestor3 ) || _inherits( block, testAncestor4 ) ) )
			return NifFieldTemplate<ModelPtr, ItemPtr>( block );
	}
	return NifFieldTemplate<ModelPtr, ItemPtr>();
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::linkBlock( const QLatin1String & testAncestor1, const QLatin1String & testAncestor2, const QLatin1String & testAncestor3, const QLatin1String & testAncestor4, const QLatin1String & testAncestor5 ) const
{
	if ( m_item ) {
		auto link = m_item->getLinkValue();
		auto block = _model()->getBlockItem( link );
		if ( block && ( _inherits( block, testAncestor1 ) || _inherits( block, testAncestor2 ) || _inherits( block, testAncestor3 ) || _inherits( block, testAncestor4 ) || _inherits( block, testAncestor5 ) ) )
			return NifFieldTemplate<ModelPtr, ItemPtr>( block );
	}
	return NifFieldTemplate<ModelPtr, ItemPtr>();
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::linkBlock( const char * testAncestor ) const
{
	return linkBlock( QLatin1String(testAncestor) );
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::linkBlock( const char * testAncestor1, const char * testAncestor2 ) const
{
	return linkBlock( QLatin1String(testAncestor1), QLatin1String(testAncestor2) );
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::linkBlock( const char * testAncestor1, const char * testAncestor2, const char * testAncestor3 ) const
{
	return linkBlock( QLatin1String(testAncestor1), QLatin1String(testAncestor2), QLatin1String(testAncestor3) );
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::linkBlock( const char * testAncestor1, const char * testAncestor2, const char * testAncestor3, const char * testAncestor4 ) const
{
	return linkBlock( QLatin1String(testAncestor1), QLatin1String(testAncestor2), QLatin1String(testAncestor3), QLatin1String(testAncestor4) );
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::linkBlock( const char * testAncestor1, const char * testAncestor2, const char * testAncestor3, const char * testAncestor4, const char * testAncestor5 ) const
{
	return linkBlock( QLatin1String(testAncestor1), QLatin1String(testAncestor2), QLatin1String(testAncestor3), QLatin1String(testAncestor4), QLatin1String(testAncestor5) );
}
template<typename ModelPtr, typename ItemPtr>
inline NifFieldTemplate<ModelPtr, ItemPtr> NifFieldTemplate<ModelPtr, ItemPtr>::linkBlock( const QStringList & testAncestors ) const
{
	if ( m_item ) {
		auto link = m_item->getLinkValue();
		auto block = _model()->getBlockItem( link );
		if ( block && _inherits( block, testAncestors ) )
			return NifFieldTemplate<ModelPtr, ItemPtr>( block );
	}
	return NifFieldTemplate<ModelPtr, ItemPtr>();
}

#endif
