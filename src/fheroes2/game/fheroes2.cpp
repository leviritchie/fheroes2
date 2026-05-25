/***************************************************************************
 *   fheroes2: https://github.com/ihhub/fheroes2                           *
 *   Copyright (C) 2019 - 2025                                             *
 *                                                                         *
 *   Free Heroes2 Engine: http://sourceforge.net/projects/fheroes2         *
 *   Copyright (C) 2009 by Andrey Afletdinov <fheroes2@gmail.com>          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <vector>

// Managing compiler warnings for SDL headers
#if defined( __GNUC__ )
#pragma GCC diagnostic push

#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wswitch-default"
#endif

#include <SDL_error.h>
#include <SDL_events.h>
#include <SDL_main.h> // IWYU pragma: keep
#include <SDL_mouse.h>

// Managing compiler warnings for SDL headers
#if defined( __GNUC__ )
#pragma GCC diagnostic pop
#endif

#if defined( _WIN32 )
#include <cassert>
#endif

#include "agg.h"
#include "agg_image.h"
#include "audio_manager.h"
#include "core.h"
#include "cursor.h"
#include "dir.h"
#include "embedded_image.h"
#include "exception.h"
#include "game.h"
#include "game_logo.h"
#include "game_video.h"
#include "game_video_type.h"
#include "h2d.h"
#include "icn.h"
#include "image.h"
#include "image_palette.h"
#include "localevent.h"
#include "logging.h"
#include "map_format_helper.h"
#include "map_format_info.h"
#include "map_random_generator.h"
#include "maps.h"
#include "math_base.h"
#include "render_processor.h"
#include "screen.h"
#include "settings.h"
#include "system.h"
#include "tinyconfig.h"
#include "timing.h"
#include "tools.h"
#include "ui_tool.h"
#include "zzlib.h"

namespace
{
    std::string GetCaption()
    {
        return std::string( "fheroes2 engine, version: " + Settings::GetVersion() );
    }

    void ReadConfigs()
    {
        const std::string configurationFileName( Settings::configFileName );
        const std::string confFile = Settings::GetLastFile( "", configurationFileName );

        Settings & conf = Settings::Get();
        if ( System::IsFile( confFile ) && conf.Read( confFile ) ) {
            LocalEvent::Get().SetControllerPointerSpeed( conf.controllerPointerSpeed() );
        }
        else {
            conf.Save( configurationFileName );

            // Fullscreen mode can be enabled by default for some devices, we need to forcibly
            // synchronize reality with the default config if config file was not read
            conf.setFullScreen( conf.FullScreen() );
        }
    }

    void InitConfigDir()
    {
        const std::string configDir = System::GetConfigDirectory( "fheroes2" );

        System::MakeDirectory( configDir );
    }

    void InitDataDir()
    {
        const std::string dataDir = System::GetDataDirectory( "fheroes2" );

        if ( dataDir.empty() ) {
            return;
        }

        const std::string dataFiles = System::concatPath( dataDir, "files" );
        const std::string dataFilesSave = System::concatPath( dataFiles, "save" );

        // This call will also create dataDir and dataFiles
        System::MakeDirectory( dataFilesSave );
    }

    void displayMissingResourceWindow()
    {
        fheroes2::Display & display = fheroes2::Display::instance();
        const fheroes2::Image & image = Compression::CreateImageFromZlib( 290, 190, errorMessage, sizeof( errorMessage ), false );

        display.fill( 0 );
        fheroes2::Resize( image, display );

        display.render();

        LocalEvent & le = LocalEvent::Get();

        // Display the message for 5 seconds so that the user sees it enough and not immediately closes without reading properly.
        const fheroes2::Time timer;

        bool closeWindow = false;

        while ( le.HandleEvents( true, true ) ) {
            if ( closeWindow && timer.getS() >= 5 ) {
                break;
            }

            if ( le.isAnyKeyPressed() || le.MouseClickLeft() ) {
                closeWindow = true;
            }
        }
    }

    class DisplayInitializer final
    {
    public:
        DisplayInitializer()
        {
            const Settings & conf = Settings::Get();

            fheroes2::Display & display = fheroes2::Display::instance();
            fheroes2::ResolutionInfo bestResolution{ conf.currentResolutionInfo() };

            if ( conf.isFirstGameRun() && System::isHandheldDevice() ) {
                // We do not show resolution dialog for first run on handheld devices. In this case it is wise to set 'widest' resolution by default.
                const std::vector<fheroes2::ResolutionInfo> resolutions = fheroes2::engine().getAvailableResolutions();

                for ( const fheroes2::ResolutionInfo & info : resolutions ) {
                    if ( info.gameWidth > bestResolution.gameWidth && info.gameHeight == bestResolution.gameHeight ) {
                        bestResolution = info;
                    }
                }
            }

            display.setWindowPos( conf.getSavedWindowPos() );
            display.setResolution( bestResolution );

            fheroes2::engine().setTitle( GetCaption() );

            // Hide system cursor.
            const int returnValue = SDL_ShowCursor( SDL_DISABLE );
            if ( returnValue < 0 ) {
                ERROR_LOG( "Failed to hide system cursor. Error description: " << SDL_GetError() )
            }

            fheroes2::RenderProcessor & renderProcessor = fheroes2::RenderProcessor::instance();

            display.subscribe( [&renderProcessor]( std::vector<uint8_t> & palette ) { return renderProcessor.preRenderAction( palette ); },
                               [&renderProcessor]() { renderProcessor.postRenderAction(); } );

            // Initialize system info renderer.
            _systemInfoRenderer = std::make_unique<fheroes2::SystemInfoRenderer>();

            renderProcessor.registerRenderers( [sysInfoRenderer = _systemInfoRenderer.get()]() { sysInfoRenderer->preRender(); },
                                               [sysInfoRenderer = _systemInfoRenderer.get()]() { sysInfoRenderer->postRender(); } );
            renderProcessor.startColorCycling();

            // Update mouse cursor when switching between software emulation and OS mouse modes.
            fheroes2::cursor().registerUpdater( Cursor::Refresh );

#if !defined( MACOS_APP_BUNDLE )
            const fheroes2::Image & appIcon = Compression::CreateImageFromZlib( 32, 32, iconImage, sizeof( iconImage ), true );
            fheroes2::engine().setIcon( appIcon );
#endif
        }

        DisplayInitializer( const DisplayInitializer & ) = delete;
        DisplayInitializer & operator=( const DisplayInitializer & ) = delete;

        ~DisplayInitializer()
        {
            fheroes2::RenderProcessor::instance().unregisterRenderers();

            fheroes2::Display & display = fheroes2::Display::instance();
            display.subscribe( {}, {} );
            display.release();
        }

    private:
        // This member must not be initialized before Display.
        std::unique_ptr<fheroes2::SystemInfoRenderer> _systemInfoRenderer;
    };

    class DataInitializer final
    {
    public:
        DataInitializer()
        {
            const fheroes2::ScreenPaletteRestorer screenRestorer;

            try {
                _aggInitializer.reset( new AGG::AGGInitializer );

                _h2dInitializer.reset( new fheroes2::h2d::H2DInitializer );

                // Verify that the font is present and it is not corrupted.
                fheroes2::AGG::GetICN( ICN::FONT, 0 );
            }
            catch ( ... ) {
                displayMissingResourceWindow();

                throw;
            }
        }

        DataInitializer( const DataInitializer & ) = delete;
        DataInitializer & operator=( const DataInitializer & ) = delete;
        ~DataInitializer() = default;

        const std::string & getOriginalAGGFilePath() const
        {
            return _aggInitializer->getOriginalAGGFilePath();
        }

        const std::string & getExpansionAGGFilePath() const
        {
            return _aggInitializer->getExpansionAGGFilePath();
        }

    private:
        std::unique_ptr<AGG::AGGInitializer> _aggInitializer;
        std::unique_ptr<fheroes2::h2d::H2DInitializer> _h2dInitializer;
    };

    // This function checks for a possible situation when a user uses a demo version
    // of the game. There is no 100% certain way to detect this, so assumptions are made.
    bool isProbablyDemoVersion()
    {
        if ( Settings::Get().isPriceOfLoyaltySupported() ) {
            return false;
        }

        // The demo version of the game only has 1 map.
        const ListFiles maps = Settings::FindFiles( "maps", ".mp2", false );
        return maps.size() == 1;
    }

#if defined( __IPHONEOS__ )
    std::string normalizeRMGConfigValue( std::string value )
    {
        value = StringLower( StringTrim( value ) );
        std::replace( value.begin(), value.end(), '-', ' ' );
        std::replace( value.begin(), value.end(), '_', ' ' );

        return value;
    }

    std::string getRandomMapGeneratorDirectory()
    {
        if ( const char * homeEnv = getenv( "HOME" ); homeEnv != nullptr ) {
            return System::concatPath( System::concatPath( homeEnv, "Documents" ), "fheroes2" );
        }

        return {};
    }

    bool getRMGInteger( const TinyConfig & config, const std::string & key, const int32_t minValue, const int32_t maxValue, int32_t & value )
    {
        if ( !config.Exists( key ) ) {
            ERROR_LOG( "Missing random map generator setting: " << key )
            return false;
        }

        const std::string text = StringTrim( config.StrParams( key ) );
        const char * first = text.data();
        const char * last = text.data() + text.size();

        int32_t parsedValue{ 0 };
        const auto [ptr, ec] = std::from_chars( first, last, parsedValue );
        if ( ptr != last || ec != std::errc() ) {
            ERROR_LOG( "Invalid random map generator integer setting '" << key << "': " << text )
            return false;
        }

        if ( parsedValue < minValue || parsedValue > maxValue ) {
            ERROR_LOG( "Random map generator setting '" << key << "' is out of range: " << parsedValue << " (" << minValue << '-' << maxValue << ')' )
            return false;
        }

        value = parsedValue;
        return true;
    }

    bool getRMGMapSize( const TinyConfig & config, int32_t & mapSize )
    {
        if ( !config.Exists( "size" ) ) {
            ERROR_LOG( "Missing random map generator setting: size" )
            return false;
        }

        const std::string size = normalizeRMGConfigValue( config.StrParams( "size" ) );
        if ( size == "small" ) {
            mapSize = Maps::SMALL;
            return true;
        }
        if ( size == "medium" ) {
            mapSize = Maps::MEDIUM;
            return true;
        }
        if ( size == "large" ) {
            mapSize = Maps::LARGE;
            return true;
        }
        if ( size == "extra large" || size == "xlarge" || size == "xl" ) {
            mapSize = Maps::XLARGE;
            return true;
        }

        int32_t numericSize{ 0 };
        if ( !getRMGInteger( config, "size", Maps::SMALL, Maps::XLARGE, numericSize ) ) {
            return false;
        }

        if ( numericSize == Maps::SMALL || numericSize == Maps::MEDIUM || numericSize == Maps::LARGE || numericSize == Maps::XLARGE ) {
            mapSize = numericSize;
            return true;
        }

        ERROR_LOG( "Unsupported random map generator map size: " << numericSize )
        return false;
    }

    bool getRMGResourceDensity( const TinyConfig & config, Maps::Random_Generator::ResourceDensity & resourceDensity )
    {
        if ( !config.Exists( "resource density" ) ) {
            ERROR_LOG( "Missing random map generator setting: resource density" )
            return false;
        }

        const std::string resources = normalizeRMGConfigValue( config.StrParams( "resource density" ) );
        if ( resources == "scarce" ) {
            resourceDensity = Maps::Random_Generator::ResourceDensity::SCARCE;
            return true;
        }
        if ( resources == "normal" ) {
            resourceDensity = Maps::Random_Generator::ResourceDensity::NORMAL;
            return true;
        }
        if ( resources == "abundant" ) {
            resourceDensity = Maps::Random_Generator::ResourceDensity::ABUNDANT;
            return true;
        }

        ERROR_LOG( "Invalid random map generator resource density: " << config.StrParams( "resource density" ) )
        return false;
    }

    bool getRMGMonsterStrength( const TinyConfig & config, Maps::Random_Generator::MonsterStrength & monsterStrength )
    {
        if ( !config.Exists( "monster strength" ) ) {
            ERROR_LOG( "Missing random map generator setting: monster strength" )
            return false;
        }

        const std::string monsters = normalizeRMGConfigValue( config.StrParams( "monster strength" ) );
        if ( monsters == "weak" ) {
            monsterStrength = Maps::Random_Generator::MonsterStrength::WEAK;
            return true;
        }
        if ( monsters == "normal" ) {
            monsterStrength = Maps::Random_Generator::MonsterStrength::NORMAL;
            return true;
        }
        if ( monsters == "strong" ) {
            monsterStrength = Maps::Random_Generator::MonsterStrength::STRONG;
            return true;
        }
        if ( monsters == "deadly" ) {
            monsterStrength = Maps::Random_Generator::MonsterStrength::DEADLY;
            return true;
        }

        ERROR_LOG( "Invalid random map generator monster strength: " << config.StrParams( "monster strength" ) )
        return false;
    }

    std::string sanitizeRMGFileName( std::string fileName )
    {
        fileName = StringTrim( std::move( fileName ) );
        for ( char & ch : fileName ) {
            switch ( ch ) {
            case '<':
            case '>':
            case ':':
            case '"':
            case '/':
            case '\\':
            case '|':
            case '?':
            case '*':
                ch = '_';
                break;
            default:
                break;
            }
        }

        return fileName;
    }

    bool getRMGMapName( const TinyConfig & config, std::string & mapName )
    {
        if ( !config.Exists( "name" ) ) {
            ERROR_LOG( "Missing random map generator setting: name" )
            return false;
        }

        mapName = sanitizeRMGFileName( config.StrParams( "name" ) );
        if ( mapName.empty() ) {
            ERROR_LOG( "Random map generator setting 'name' must not be empty." )
            return false;
        }

        return true;
    }

    void createRandomMapGeneratorTemplate( const std::string & dataDir )
    {
        if ( dataDir.empty() ) {
            return;
        }

        const std::string templatePath = System::concatPath( dataDir, "rmg-template.cfg" );
        if ( System::IsFile( templatePath ) ) {
            return;
        }

        std::ofstream templateFile( templatePath );
        if ( !templateFile ) {
            ERROR_LOG( "Unable to create random map generator template: " << templatePath )
            return;
        }

        templateFile << "# Rename or copy this file to rmg.cfg to generate random maps on next launch.\n"
                     << "# The app deletes rmg.cfg after a successful generation to avoid duplicates.\n"
                     << "enabled = off\n"
                     << "count = 1\n"
                     << "size = medium\n"
                     << "players = 2\n"
                     << "water = 0\n"
                     << "seed = 0\n"
                     << "resource density = normal\n"
                     << "monster strength = normal\n"
                     << "name = Random map\n";
    }

    void processHeadlessRandomMapGeneratorRequest()
    {
        const std::string rmgDir = getRandomMapGeneratorDirectory();
        if ( !rmgDir.empty() && !System::IsDirectory( rmgDir ) && !System::MakeDirectory( rmgDir ) ) {
            ERROR_LOG( "Unable to create random map generator directory: " << rmgDir )
            return;
        }

        createRandomMapGeneratorTemplate( rmgDir );

        if ( rmgDir.empty() ) {
            return;
        }

        const std::string requestPath = System::concatPath( rmgDir, "rmg.cfg" );
        if ( !System::IsFile( requestPath ) ) {
            return;
        }

        TinyConfig config( '=', '#' );
        if ( !config.Load( requestPath ) ) {
            ERROR_LOG( "Unable to read random map generator request: " << requestPath )
            return;
        }

        const std::string enabled = normalizeRMGConfigValue( config.StrParams( "enabled" ) );
        if ( enabled.empty() ) {
            ERROR_LOG( "Missing random map generator setting: enabled" )
            return;
        }
        if ( enabled == "off" ) {
            return;
        }
        if ( enabled != "on" ) {
            ERROR_LOG( "Invalid random map generator enabled setting: " << config.StrParams( "enabled" ) )
            return;
        }

        if ( !Settings::Get().isPriceOfLoyaltySupported() ) {
            ERROR_LOG( "Random map generation requires Price of Loyalty data. Import HEROES2X.AGG next to HEROES2.AGG before using rmg.cfg." )
            return;
        }

        int32_t mapSize{ 0 };
        if ( !getRMGMapSize( config, mapSize ) ) {
            return;
        }

        Maps::Random_Generator::Configuration rmgConfig;
        if ( !getRMGInteger( config, "players", 2, 6, rmgConfig.playerCount ) ) {
            return;
        }
        if ( !getRMGInteger( config, "water", 0, Maps::Random_Generator::calculateMaximumWaterPercentage( rmgConfig.playerCount, mapSize ),
                              rmgConfig.waterPercentage ) ) {
            return;
        }
        if ( !getRMGInteger( config, "seed", 0, 999999, rmgConfig.seed ) ) {
            return;
        }
        if ( !getRMGResourceDensity( config, rmgConfig.resourceDensity ) ) {
            return;
        }
        if ( !getRMGMonsterStrength( config, rmgConfig.monsterStrength ) ) {
            return;
        }

        int32_t mapCount{ 0 };
        if ( !getRMGInteger( config, "count", 1, 20, mapCount ) ) {
            return;
        }
        std::string baseName;
        if ( !getRMGMapName( config, baseName ) ) {
            return;
        }

        std::string mapDirectory = System::concatPath( rmgDir, "maps" );
        if ( !System::IsDirectory( mapDirectory ) && !System::MakeDirectory( mapDirectory ) ) {
            ERROR_LOG( "Unable to create maps directory for random map generator output: " << mapDirectory )
            return;
        }

        if ( !System::GetCaseInsensitivePath( mapDirectory, mapDirectory ) ) {
            ERROR_LOG( "Unable to locate maps directory for random map generator output: " << mapDirectory )
            return;
        }

        for ( int32_t index = 0; index < mapCount; ++index ) {
            Maps::Map_Format::MapFormat mapFormat;
            Maps::Random_Generator::Configuration currentConfig = rmgConfig;
            if ( rmgConfig.seed > 0 ) {
                currentConfig.seed += index;
            }

            if ( !Maps::Random_Generator::generateMap( mapFormat, currentConfig, mapSize, mapSize ) ) {
                ERROR_LOG( "Failed to generate random map from request: " << requestPath )
                return;
            }

            if ( !baseName.empty() ) {
                mapFormat.name = ( mapCount == 1 ) ? baseName : baseName + " " + std::to_string( index + 1 );
            }

            if ( !Maps::updateMapPlayers( mapFormat ) ) {
                ERROR_LOG( "Generated random map is invalid and was not saved." )
                return;
            }

            const std::string fileName = sanitizeRMGFileName( mapFormat.name ) + ".fh2m";
            const std::string outputPath = System::concatPath( mapDirectory, fileName );
            if ( !Maps::Map_Format::saveMap( outputPath, mapFormat ) ) {
                ERROR_LOG( "Failed to save generated random map: " << outputPath )
                return;
            }

            DEBUG_LOG( DBG_GAME, DBG_INFO, "Generated random map: " << outputPath )
        }

        if ( !System::Unlink( requestPath ) ) {
            ERROR_LOG( "Generated random maps, but failed to remove request file: " << requestPath )
        }
    }
#endif
}

int main( int argc, char ** argv )
{
// SDL2main.lib converts argv to UTF-8, but this application expects ANSI, use the original argv
#if defined( _WIN32 )
    assert( argc == __argc );

    argv = __argv;
#else
    (void)argc;
#endif

    try {
        const fheroes2::HardwareInitializer hardwareInitializer;
        Logging::InitLog();

        COUT( GetCaption() )

        Settings & conf = Settings::Get();
        conf.SetProgramPath( argv[0] );

        InitConfigDir();
        InitDataDir();
        ReadConfigs();

        std::set<fheroes2::SystemInitializationComponent> coreComponents{ fheroes2::SystemInitializationComponent::Audio,
                                                                          fheroes2::SystemInitializationComponent::Video };

#if defined( TARGET_PS_VITA ) || defined( TARGET_NINTENDO_SWITCH )
        coreComponents.emplace( fheroes2::SystemInitializationComponent::GameController );
#endif

        const fheroes2::CoreInitializer coreInitializer( coreComponents );

        DEBUG_LOG( DBG_GAME, DBG_INFO, conf.String() )

        const DisplayInitializer displayInitializer;
        const DataInitializer dataInitializer;

        ListFiles midiSoundFonts;
        {
            const std::string path = System::concatPath( "files", "soundfonts" );
            midiSoundFonts.Append( Settings::FindFiles( path, ".sf2", false ) );
            midiSoundFonts.Append( Settings::FindFiles( path, ".sf3", false ) );
        }

#ifdef WITH_DEBUG
        for ( const std::string & file : midiSoundFonts ) {
            DEBUG_LOG( DBG_GAME, DBG_INFO, "MIDI SoundFont to load: " << file )
        }
#endif

        const std::string timidityCfgPath = []() -> std::string {
            if ( std::string path; Settings::findFile( System::concatPath( "files", "timidity" ), "timidity.cfg", path ) ) {
                return path;
            }

            return {};
        }();

#ifdef WITH_DEBUG
        if ( !timidityCfgPath.empty() ) {
            DEBUG_LOG( DBG_GAME, DBG_INFO, "Path to the timidity.cfg file: " << timidityCfgPath )
        }
#endif

        const AudioManager::AudioInitializer audioInitializer( dataInitializer.getOriginalAGGFilePath(), dataInitializer.getExpansionAGGFilePath(), midiSoundFonts,
                                                               timidityCfgPath );

        // Load palette.
        fheroes2::setGamePalette( AGG::getDataFromAggFile( "KB.PAL", false ) );
        const fheroes2::Display & display = fheroes2::Display::instance();
        display.changePalette( nullptr, true );

        // Update the fonts according to the game language set in the configuration.
        // NOTICE: it must be done before initializing the engine to properly load all
        // language-specific font characters for the selected language because during
        // initialization the English language is forced to properly read the configuration files.
        conf.setGameLanguage( conf.getGameLanguage() );

        // Initialize game data.
        Game::Init();

#if defined( __IPHONEOS__ )
        processHeadlessRandomMapGeneratorRequest();
#endif

        if ( conf.isShowIntro() ) {
            fheroes2::showTeamInfo();
            for ( const char * logo : { "NWCLOGO.SMK", "CYLOGO.SMK", "H2XINTRO.SMK" } ) {
                Video::ShowVideo( { { logo, Video::VideoControl::PLAY_CUTSCENE } } );
            }
        }

        try {
            const CursorRestorer cursorRestorer( true, Cursor::POINTER );
            const fheroes2::Point pos = conf.getSavedWindowPos();
            Game::mainGameLoop( conf.isFirstGameRun(), isProbablyDemoVersion() );
            const fheroes2::Point currentPos = display.getWindowPos();
            if ( pos != currentPos ) {
                conf.setStartWindowPos( currentPos );
                conf.Save( Settings::configFileName );
            }
        }
        catch ( const fheroes2::InvalidDataResources & ex ) {
            ERROR_LOG( ex.what() )
            displayMissingResourceWindow();
            return EXIT_FAILURE;
        }
    }
    catch ( const std::exception & ex ) {
        ERROR_LOG( "Exception '" << ex.what() << "' occurred during application runtime." )
        return EXIT_FAILURE;
    }
    catch ( ... ) {
        ERROR_LOG( "An unknown exception occurred during application runtime." )
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
