/*
 * Copyright 2014-2016, Corvusoft Ltd, All Rights Reserved.
 */

//System Includes
#include <set>
#include <string>
#include <memory>
#include <algorithm>

//Project Includes
#include "stl_repository.hpp"

//External Includes
#include <corvusoft/restq/string.hpp>
#include <corvusoft/restq/status_code.hpp>

//System Namespaces
using std::set;
using std::pair;
using std::mutex;
using std::size_t;
using std::vector;
using std::string;
using std::advance;
using std::find_if;
using std::function;
using std::multimap;
using std::shared_ptr;
using std::unique_lock;

//Project Namespaces

//External Namespaces
using restq::OK;
using restq::CREATED;
using restq::CONFLICT;
using restq::NOT_FOUND;
using restq::NO_CONTENT;
using restq::Bytes;
using restq::Query;
using restq::String;
using restq::Logger;
using restq::Settings;
using restq::Resource;
using restq::Resources;
using restq::Repository;

STLRepository::STLRepository( void ) : Repository( ),
    m_resources_lock( ),
    m_resources( )
{
    return;
}

STLRepository::~STLRepository( void )
{
    return;
}

void STLRepository::stop( void )
{
    return;
}

void STLRepository::start( const shared_ptr< const Settings >& )
{
    return;
}

void STLRepository::create( const Resources values, const shared_ptr< Query > query, const function< void ( const int, const Resources, const shared_ptr< Query > ) >& callback )
{
    unique_lock< mutex> lock( m_resources_lock );
    
    for ( const auto value : values )
    {
        bool conflict = any_of( m_resources.begin( ), m_resources.end( ), [ &value ]( const Resource & resource )
        {
            const auto lhs = String::to_string( value.lower_bound( "key" )->second );
            const auto rhs = String::to_string( resource.lower_bound( "key" )->second );
            
            return String::lowercase( lhs ) == String::lowercase( rhs );
        } );
        
        if ( conflict )
        {
            return callback( CONFLICT, values, query );
        }
    }
    
    m_resources.insert( m_resources.end( ), values.begin( ), values.end( ) );
    
    lock.unlock( );
    
    callback( CREATED, values, query );
}

void STLRepository::read( const shared_ptr< Query > query, const function< void ( const int, const Resources, const shared_ptr< Query > ) >& callback )
{
    Resources values;
    Resources resources;
    const auto keys = query->get_keys( );
    
    unique_lock< mutex> lock( m_resources_lock );
    
    if ( not keys.empty( ) )
    {
        for ( const auto& key : keys )
        {
            auto resource = find_if( m_resources.begin( ), m_resources.end( ), [ &key ]( const Resource & resource )
            {
                if ( resource.count( "key" ) == 0 )
                {
                    return false;
                }
                
                return String::lowercase( key ) == String::lowercase( String::to_string( resource.lower_bound( "key" )->second ) );
            } );
            
            if ( resource == m_resources.end( ) )
            {
                return callback( NOT_FOUND, values, query );
            }
            
            resources.push_back( *resource );
        }
    }
    else
    {
        resources = m_resources;
    }
    
    lock.unlock( );
    
    filter( resources, query->get_inclusive_filters( ), query->get_exclusive_filters( ) ); //just pass query
    
    const auto& index = query->get_index( );
    const auto& limit = query->get_limit( );
    
    if ( index < resources.size( ) and limit not_eq 0 )
    {
        auto start = resources.begin( );
        advance( start, index );
        
        while ( start not_eq resources.end( ) and limit not_eq values.size( ) )
        {
            values.push_back( *start );
            start++;
        }
    }
    
    include( query->get_include( ), values ); //just pass query
    
    callback( OK, values, query );
}

void STLRepository::update( const Resource changeset, const shared_ptr< Query > query, const function< void (  const int, const Resources, const shared_ptr< Query > ) >& callback  )
{
    read( query, [ changeset, callback, this ]( const int status_code, const Resources values, const shared_ptr< Query > query )
    {
        if ( status_code not_eq OK )
        {
            return callback( status_code, values, query );
        }
        
        int status = NO_CONTENT;
        
        auto resources = values;
        
        unique_lock< mutex> lock( m_resources_lock );
        
        for ( auto& value : resources )
        {
            for ( const auto& change : changeset )
            {
                if ( change.first == "modified" or change.first == "revision" )
                {
                    continue;
                }
                
                auto property = value.find( change.first );
                
                if ( property == value.end( ) )
                {
                    value.insert( change );
                    status = OK;
                }
                else if ( property->second not_eq change.second )
                {
                    property->second = change.second;
                    status = OK;
                }
            }
            
            auto resource = find_if( m_resources.begin( ), m_resources.end( ), [ &value ]( const Resource & resource )
            {
                const auto lhs = String::to_string( value.lower_bound( "key" )->second );
                const auto rhs = String::to_string( resource.lower_bound( "key" )->second );
                
                return String::lowercase( lhs ) == String::lowercase( rhs );
            } );
            
            *resource = value;
        }
        
        lock.unlock( );
        
        callback( status, resources, query );
    } );
}

void STLRepository::destroy( const shared_ptr< Query > query, const function< void ( const int, const shared_ptr< Query > ) >& callback )
{
    read( query, [ callback, this ]( const int status, const Resources resources, const shared_ptr< Query > query )
    {
        if ( status not_eq OK )
        {
            return callback( status, query );
        }
        
        unique_lock< mutex> lock( m_resources_lock );
        
        for ( const auto resource : resources )
        {
            auto iterator = find_if( m_resources.begin( ), m_resources.end( ), [ &resource ]( const Resource & value )
            {
                const auto lhs = String::to_string( value.lower_bound( "key" )->second );
                const auto rhs = String::to_string( resource.lower_bound( "key" )->second );
                
                return String::lowercase( lhs ) == String::lowercase( rhs );
            } );
            
            m_resources.erase( iterator );
        }
        
        lock.unlock( );
        
        callback( OK, query );
    } );
}

void STLRepository::set_logger( const shared_ptr< Logger >& )
{
    return;
}

void STLRepository::include( const Bytes& relationship, Resources& values )
{
    if ( relationship.empty( ) )
    {
        return;
    }
    
    Resources relationships;
    
    unique_lock< mutex> lock( m_resources_lock );
    
    if ( relationship == restq::SUBSCRIPTION )
    {
        for ( const auto& resource : m_resources )
        {
            if ( resource.lower_bound( "type" )->second == restq::SUBSCRIPTION )
            {
                auto iterators = resource.equal_range( "queues" );
                
                for ( const auto& queue : values )
                {
                    auto key = String::lowercase( String::to_string( queue.lower_bound( "key" )->second ) );
                    
                    for ( auto iterator = iterators.first; iterator not_eq iterators.second; iterator++ )
                    {
                        if ( key == String::lowercase( String::to_string( iterator->second ) ) )
                        {
                            relationships.push_back( resource );
                            break;
                        }
                    }
                }
            }
        }
    }
    else
    {
        for ( const auto& value : values )
        {
            const auto key = String::lowercase( String::to_string( value.lower_bound( "key" )->second ) );
            const auto type = value.lower_bound( "type" )->second;
            const auto foreign_key = String::to_string( type ) + "-key";
            
            for ( const auto& resource : m_resources )
            {
                if ( resource.lower_bound( "type" )->second == relationship )
                {
                    if ( resource.count( foreign_key ) and key == String::lowercase( String::to_string( resource.lower_bound( foreign_key )->second ) ) )
                    {
                        relationships.push_back( resource );
                    }
                }
            }
        }
    }
    
    lock.unlock( );
    
    values.insert( values.end( ), relationships.begin( ), relationships.end( ) );
}

void STLRepository::filter( Resources& resources, const multimap< string, Bytes >& inclusive_filters, const multimap< string, Bytes >& exclusive_filters ) const
{
    for ( auto resource = resources.begin( ); resource not_eq resources.end( ); resource++ )
    {
        bool failed = true;
        
        for ( const auto filter : exclusive_filters )
        {
            failed = true;
            
            const auto properties = resource->equal_range( filter.first );
            
            for ( auto property = properties.first; property not_eq properties.second; property++ )
            {
                if ( property->second == filter.second )
                {
                    failed = false;
                    break;
                }
            }
            
            if ( failed )
            {
                break;
            }
        }
        
        if ( failed and not exclusive_filters.empty( ) )
        {
            resources.erase( resource );
            continue;
        }
        
        
        
        if ( inclusive_filters.empty( ) )
        {
            continue;
        }
        
        failed = true;
        
        for ( auto filter_iterator = inclusive_filters.begin( ); filter_iterator not_eq inclusive_filters.end( ); filter_iterator++ )
        {
            failed = true;
            
            const auto properties = resource->equal_range( filter_iterator->first );
            const auto filters = inclusive_filters.equal_range( filter_iterator->first );
            
            for ( auto filter = filters.first; filter not_eq filters.second; filter++ )
            {
                for ( auto property = properties.first; property not_eq properties.second; property++ )
                {
                    if ( property->second == filter->second )
                    {
                        failed = false;
                        break;
                    }
                }
                
                if ( not failed )
                {
                    break;
                }
            }
            
            if ( failed )
            {
                break;
            }
            
            if ( filters.second not_eq inclusive_filters.end( ) )
            {
                filter_iterator = filters.second;
            }
        }
        
        if ( failed )
        {
            resources.erase( resource );
        }
    }
}
