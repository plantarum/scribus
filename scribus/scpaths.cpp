/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/
#include "scpaths.h"
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QProcess>

#include "scconfig.h"

// On Qt/Mac we need CoreFoundation to discover the location
// of the app bundle.
#ifdef Q_OS_MAC
#include <CoreFoundation/CoreFoundation.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

#ifdef _WIN32
const char ScPaths::envPathSeparator = ';';
#else
const char ScPaths::envPathSeparator = ':';
#endif

// Init the singleton's "self" address to NULL
ScPaths* ScPaths::m_instance = NULL;

// Singleton's public constructor
const ScPaths& ScPaths::instance()
{
	if (!ScPaths::m_instance)
		ScPaths::m_instance = new ScPaths();
	return *ScPaths::m_instance;
}

// Singleton's public destructor
void ScPaths::destroy()
{
	if (ScPaths::m_instance)
		delete ScPaths::m_instance;
}

// Protected "real" constructor
// All paths are initialized to compile-time defaults passed in
// as preprocessor macros and set by autoconf.
ScPaths::ScPaths() :
	m_docDir(DOCDIR),
	m_iconDir(ICONDIR),
	m_libDir(LIBDIR),
	m_pluginDir(PLUGINDIR),
	m_qmlDir(QMLDIR),
	m_sampleScriptDir(SAMPLESDIR),
	m_scriptDir(SCRIPTSDIR),
	m_shareDir(SHAREDIR),
	m_templateDir(TEMPLATEDIR)
{
// On MacOS/X, override the compile-time settings with a location
// obtained from the system.
#ifdef Q_OS_MAC
	QString pathPtr(bundleDir());
	qDebug() << QString("scpaths: bundle at %1").arg(pathPtr);
	m_shareDir = QString("%1/Contents/share/scribus/").arg(pathPtr);
	m_docDir = QString("%1/Contents/share/doc/scribus/").arg(pathPtr);
	m_iconDir = QString("%1/Contents/share/scribus/icons/").arg(pathPtr);
	m_sampleScriptDir = QString("%1/Contents/share/scribus/samples/").arg(pathPtr);
	m_scriptDir = QString("%1/Contents/share/scribus/scripts/").arg(pathPtr);
	m_templateDir = QString("%1/Contents/share/scribus/templates/").arg(pathPtr);
	m_libDir = QString("%1/Contents/lib/scribus/").arg(pathPtr);
	m_pluginDir = QString("%1/Contents/lib/scribus/plugins/").arg(pathPtr);
	m_qmlDir = QString("%1/Contents/share/scribus/qml/").arg(pathPtr);
	//QApplication::setLibraryPaths(QStringList(QString("%1/Contents/lib/qtplugins/").arg(pathPtr)));
	QApplication::addLibraryPath(QString("%1/Contents/lib/qtplugins/").arg(pathPtr));
	// on OSX this goes to the sys console, so user only sees it when they care -- AV
	qDebug() << QString("scpaths: doc dir=%1").arg(m_docDir);
	qDebug() << QString("scpaths: icon dir=%1").arg(m_iconDir);
	qDebug() << QString("scpaths: font dir=%1").arg(m_fontDir);
	qDebug() << QString("scpaths: sample dir=%1").arg(m_sampleScriptDir);
	qDebug() << QString("scpaths: script dir=%1").arg(m_scriptDir);
	qDebug() << QString("scpaths: template dir=%1").arg(m_templateDir);
	qDebug() << QString("scpaths: lib dir=%1").arg(m_libDir);
	qDebug() << QString("scpaths: plugin dir=%1").arg(m_pluginDir);
	qDebug() << QString("scpaths: QML dir=%1").arg(m_qmlDir);
	qDebug() << QString("scpaths: qtplugins=%1").arg(QApplication::libraryPaths().join(":"));

#elif defined(_WIN32)
	QString appPath = qApp->applicationDirPath();
	m_shareDir = QString("%1/share/").arg(appPath);
	m_docDir = QString("%1/share/doc/").arg(appPath);
	m_fontDir = QString("%1/share/fonts/").arg(appPath);
	m_iconDir = QString("%1/share/icons/").arg(appPath);
	m_sampleScriptDir = QString("%1/share/samples/").arg(appPath);
	m_scriptDir = QString("%1/share/scripts/").arg(appPath);
	m_templateDir = QString("%1/share/templates/").arg(appPath);
	m_libDir = QString("%1/libs/").arg(appPath);
	m_pluginDir = QString("%1/plugins/").arg(appPath);
	m_qmlDir = QString("%1/share/qml/").arg(appPath);

	QString qtpluginDir = QString("%1/qtplugins/").arg(appPath);
	if (QDir(qtpluginDir).exists())
		QApplication::setLibraryPaths( QStringList(qtpluginDir) );
#endif
	
// 	if(!m_shareDir.endsWith("/"))        m_shareDir += "/";
// 	if(!m_docDir.endsWith("/"))          m_docDir += "/";
// 	if(!m_fontDir.endsWith("/"))         m_fontDir += "/";
	if(!m_iconDir.endsWith("/"))         m_iconDir += "/";
// 	if(!m_sampleScriptDir.endsWith("/")) m_sampleScriptDir += "/";
// 	if(!m_scriptDir.endsWith("/"))       m_scriptDir += "/";
// 	if(!m_templateDir.endsWith("/"))     m_templateDir += "/";
// 	if(!m_libDir.endsWith("/"))          m_libDir += "/";
// 	if(!m_pluginDir.endsWith("/"))       m_pluginDir += "/";
}

ScPaths::~ScPaths() {};

QString ScPaths::bundleDir(void) const
{
	// On MacOS/X, override the compile-time settings with a location
// obtained from the system.
#ifdef Q_OS_MAC
	// Set up the various app paths to look inside the app bundle
	CFURLRef pluginRef = CFBundleCopyBundleURL(CFBundleGetMainBundle());
	CFStringRef macPath = CFURLCopyFileSystemPath(pluginRef, kCFURLPOSIXPathStyle);
	const char *pathPtr = CFStringGetCStringPtr(macPath, CFStringGetSystemEncoding());
	if (pathPtr!=NULL && strlen(pathPtr)>0)
	{
		// make sure we get the Scribus.app directory, not some subdir
		// strip trailing '/':
		qDebug("Path = %s", pathPtr);
		char *p = const_cast<char*>(pathPtr + strlen(pathPtr) - 1);
		while (*p == '/')
			--p;
		++p;
		*p = '\0';
		if (strcmp("/bin", p-4) == 0) {
			p -= 4;
			*p = '\0';
		}
		if (strcmp("/MacOS", p-6) == 0) {
			p -= 6;
			*p = '\0';
		}
		if (strcmp("/Contents", p-9) == 0) {
			p -= 9;
			*p = '\0';
		}
		CFRelease(pluginRef);
		CFRelease(macPath);
		return QString("%1").arg(pathPtr);
	}
	else
	{
		char buf[2048];
		CFStringGetCString (macPath, buf, 2048, kCFStringEncodingUTF8);
		QString q_pathPtr=QString::fromUtf8(buf);
		if (q_pathPtr.endsWith("/bin"))
			q_pathPtr.chop(4);
		if (q_pathPtr.endsWith("/MacOS"))
			q_pathPtr.chop(6);
		if (q_pathPtr.endsWith("/Contents"))
			q_pathPtr.chop(9);
		CFRelease(pluginRef);
		CFRelease(macPath);
		return q_pathPtr;
	}
#endif
	return QString::null;
}

const QString&  ScPaths::docDir() const
{
	return m_docDir;
}

const QString&  ScPaths::iconDir() const
{
	return m_iconDir;
}

const QString&  ScPaths::fontDir() const
{
	return m_fontDir;
}

const QString&  ScPaths::libDir() const
{
	return m_libDir;
}

const QString&  ScPaths::pluginDir() const
{
	return m_pluginDir;
}

const QString&  ScPaths::sampleScriptDir() const
{
	return m_sampleScriptDir;
}

const QString&  ScPaths::scriptDir() const
{
	return m_scriptDir;
}

const QString&  ScPaths::templateDir() const
{
	return m_templateDir;
}

const QString&  ScPaths::shareDir() const
{
	return m_shareDir;
}

const QString&  ScPaths::qmlDir() const
{
	return m_qmlDir;
}

QString ScPaths::translationDir() const
{
	return (m_shareDir + "translations/");
}

QString ScPaths::dictDir() const
{
	return(m_shareDir + "dicts/");
}

QStringList ScPaths::spellDirs() const
{
	//dictionaryPaths
	QString macPortsPath("/opt/local/share/hunspell/");
	QString finkPath("/sw/share/hunspell/");
	QString osxLibreOfficePath("/Applications/LibreOffice.app/Contents/share/extensions");
	QString osxUserLibreOfficePath(QDir::homePath()+"/Applications/LibreOffice.app/Contents/share/extensions");
	QString linuxLocalPath("/usr/local/share/hunspell/");
	QString linuxHunspellPath("/usr/share/hunspell/");
	QString linuxMyspellPath("/usr/share/myspell/");
	QString windowsLOPath("LibreOffice 3.5/share/extensions");
	QDir d;
	QStringList spellDirs;
	spellDirs.append(getUserDictDir(false));
	spellDirs.append(m_shareDir + "dicts/spelling/");
#ifdef Q_OS_MAC
	d.setPath(macPortsPath);
	if (d.exists())
		spellDirs.append(macPortsPath);
	d.setPath(finkPath);
	if (d.exists())
		spellDirs.append(finkPath);
	d.setPath(osxLibreOfficePath);
	if (d.exists())
	{
		QStringList dictDirFilters("dict-*");
		QStringList dictDirList(d.entryList(dictDirFilters, QDir::Dirs, QDir::Name));
		QString dir;
		foreach (dir, dictDirList)
			spellDirs.append(osxLibreOfficePath + "/" + dir + "/");
	}
	d.setPath(osxUserLibreOfficePath);
	if (d.exists())
	{
		QStringList dictDirFilters("dict-*");
		QStringList dictDirList(d.entryList(dictDirFilters, QDir::Dirs, QDir::Name));
		QString dir;
		foreach (dir, dictDirList)
			spellDirs.append(osxUserLibreOfficePath + "/" + dir + "/");
	}

#elif defined(_WIN32)
	QString progFiles = getSpecialDir(CSIDL_PROGRAM_FILES);
	d.setPath(progFiles+windowsLOPath);
	if (d.exists())
	{
		QStringList dictDirFilters("dict-*");
		QStringList dictDirList(d.entryList(dictDirFilters, QDir::Dirs, QDir::Name));
		QString dir;
		foreach (dir, dictDirList)
			spellDirs.append(progFiles+windowsLOPath + "/" + dir + "/");
	}
#elif defined(Q_OS_LINUX)
	d.setPath(linuxHunspellPath);
	if (d.exists())
		spellDirs.append(linuxHunspellPath);
	d.setPath(linuxMyspellPath);
	if (d.exists())
		spellDirs.append(linuxMyspellPath);
	d.setPath(linuxLocalPath);
	if (d.exists())
		spellDirs.append(linuxLocalPath);
#endif
	return spellDirs;
}

QStringList ScPaths::getSystemFontDirs(void)
{
	QStringList fontDirs;
#ifdef Q_OS_MAC
	fontDirs.append(QDir::homePath() + "/Library/Fonts/");
	fontDirs.append("/Library/Fonts/");
	fontDirs.append("/Network/Library/Fonts/");
	fontDirs.append("/System/Library/Fonts/");
#elif defined(_WIN32)
	fontDirs.append( getSpecialDir(CSIDL_FONTS) );
#endif
	return fontDirs;
}

QStringList ScPaths::getSystemProfilesDirs(void)
{
	QStringList iccProfDirs;
#ifdef Q_OS_MAC
	iccProfDirs.append(QDir::homePath()+"/Library/ColorSync/Profiles/");
	iccProfDirs.append("/System/Library/ColorSync/Profiles/");
	iccProfDirs.append("/Library/ColorSync/Profiles/");
#elif defined(Q_OS_LINUX)
	iccProfDirs.append(QDir::homePath()+"/color/icc/");
	iccProfDirs.append(QDir::homePath()+"/.color/icc/");
	iccProfDirs.append(QDir::homePath()+"/.local/share/icc/");
	iccProfDirs.append(QDir::homePath()+"/.local/share/color/icc/");
	iccProfDirs.append("/usr/share/color/icc/");
	iccProfDirs.append("/usr/local/share/color/icc/");
	iccProfDirs.append("/var/lib/color/icc/");
#elif defined(_WIN32)
	// On Windows it's more complicated, profiles location depends on OS version
	WCHAR sysDir[MAX_PATH + 1];
	OSVERSIONINFO osVersion;
	ZeroMemory( &osVersion, sizeof(OSVERSIONINFO));
	osVersion.dwOSVersionInfoSize = sizeof(OSVERSIONINFO); // Necessary for GetVersionEx to succeed
	GetVersionEx(&osVersion);  // Get Windows version infos
	GetSystemDirectoryW( sysDir, MAX_PATH ); // getSpecialDir(CSIDL_SYSTEM) fails on Win9x
	QString winSysDir = QString::fromUtf16((const ushort*) sysDir);
	winSysDir = winSysDir.replace('\\','/');
	if( osVersion.dwPlatformId == VER_PLATFORM_WIN32_NT	) // Windows NT/2k/XP
	{
		if( osVersion.dwMajorVersion >= 5 ) // for 2k and XP dwMajorVersion == 5 
			iccProfDirs.append( winSysDir + "/Spool/Drivers/Color/");
	}
	else if( osVersion.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS ) // Windows 9x/Me 
	{
		if( osVersion.dwMajorVersion >= 4 && osVersion.dwMinorVersion >= 10) // Win98 or WinMe
			iccProfDirs.append( winSysDir + "/Color/");
	}
#endif
	return iccProfDirs;
}

QStringList ScPaths::getDirsFromEnvVar(const QString envVar, const QString dirToFind)
{
	QChar sep(':');
#ifdef _WIN32
	sep=';';
#endif
	QStringList dirs;
#if defined(Q_OS_MAC) || defined(Q_OS_UNIX)
	QStringList env(QProcess::systemEnvironment());
	QString path_data;
	foreach (QString line, env)
	{
		if (line.indexOf(envVar) == 0)
			path_data = line.mid(envVar.length()+1); //eg, Strip "XDG_DATA_DIRS="
	}
	QStringList splitpath_data(path_data.split(sep, QString::SkipEmptyParts));
	foreach (QString dir, splitpath_data)
	{
		QFileInfo info(dir+dirToFind);
		if (info.exists())
			dirs.append(dir+dirToFind);
	}
#endif
	return dirs;
}


QStringList ScPaths::getSystemCreateSwatchesDirs(void)
{
	QStringList createDirs;
#ifdef Q_OS_MAC
	createDirs.append(QDir::homePath()+"/create/swatches/");
	createDirs.append(QDir::homePath()+"/.create/swatches/");
#elif defined(Q_OS_LINUX)
	createDirs.append(QDir::homePath()+"/create/swatches/");
	createDirs.append(QDir::homePath()+"/.create/swatches/");
	createDirs.append("/usr/share/create/swatches/");
	createDirs.append("/usr/local/share/create/swatches/");
#elif defined(_WIN32)
	QString localAppData = getSpecialDir(CSIDL_LOCAL_APPDATA);
	QString commonAppData = getSpecialDir(CSIDL_COMMON_APPDATA);
	QString programFilesCommon = getSpecialDir(CSIDL_PROGRAM_FILES_COMMON);
	createDirs.append(getSpecialDir(CSIDL_APPDATA) + "create/swatches/");
	if ( !localAppData.isEmpty() )
		createDirs.append(localAppData + "create/swatches/");
	if ( !commonAppData.isEmpty() )
		createDirs.append(commonAppData + "create/swatches/");
	if ( !programFilesCommon.isEmpty() )
		createDirs.append(programFilesCommon + "create/swatches/");
#endif
	return createDirs;
}

QString ScPaths::getApplicationDataDir(void)
{
#if defined(_WIN32)
	QString appData = getSpecialDir(CSIDL_APPDATA);
	if (QDir(appData).exists())
#ifdef APPLICATION_DATA_DIR
		return (appData + "/" + APPLICATION_DATA_DIR + "/");
#else
		return (appData + "/Scribus/");
#endif
#endif

#ifdef APPLICATION_DATA_DIR
	return QDir::homePath() + "/" + APPLICATION_DATA_DIR + "/";
#else
	#ifdef Q_OS_MAC
		return (QDir::homePath() + "/Library/Preferences/Scribus/");
	#else
		return (QDir::homePath() + "/.scribus/");
	#endif
#endif
}

QString ScPaths::getImageCacheDir(void)
{
	return getApplicationDataDir() + "cache/img/";
}

QString ScPaths::getPluginDataDir(void)
{
	return getApplicationDataDir() + "plugins/";
}

QString ScPaths::getUserDictDir(bool createIfNotExists)
{
	QDir userDictDirectory(getApplicationDataDir() + "dicts/");
	if(createIfNotExists)
	{
		if (!userDictDirectory.exists())
			userDictDirectory.mkpath(userDictDirectory.absolutePath());
	}
	return userDictDirectory.absolutePath()+"/";
}

QString ScPaths::getUserDocumentDir(void)
{
#if defined(_WIN32)
	QString userDocs = getSpecialDir(CSIDL_PERSONAL);
	if	(QDir(userDocs).exists())
		return userDocs;
#endif
	return (QDir::homePath() + "/");
}

QString ScPaths::getTempFileDir(void)
{
#if defined(_WIN32)
	QString tempPath;
	WCHAR wTempPath[1024];
	DWORD result = GetTempPathW(1024, wTempPath);
	if ( result )
	{
		tempPath = QString::fromUtf16((const unsigned short*) wTempPath);
		tempPath.replace( '\\', '/' );
		tempPath += "/";
		// GetTempPath may return Windows directory, better not use this one
		// for temporary files
		if (QDir(tempPath).exists() && tempPath != getSpecialDir(CSIDL_WINDOWS))
			return tempPath;
	}
#endif
	return getApplicationDataDir();
}

QString ScPaths::downloadDir()
{
	QDir downloadDirectory(getApplicationDataDir() + "downloads/");
	if (!downloadDirectory.exists())
		downloadDirectory.mkpath(downloadDirectory.absolutePath());
	return downloadDirectory.absolutePath()+"/";
}

QString ScPaths::getSpecialDir(int folder)
{
	QString qstr;
#if defined(_WIN32)
	WCHAR dir[256];
	if ( SHGetSpecialFolderPathW(NULL, dir, folder , false) )
	{
		qstr = QString::fromUtf16((const unsigned short*) dir);
		if( !qstr.endsWith("\\") )
			qstr += "\\";
		qstr.replace( '\\', '/' );
	}
#else
	Q_ASSERT(false);
#endif
	return qstr;
}
