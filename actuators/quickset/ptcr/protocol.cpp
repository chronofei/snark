// This file is part of snark, a generic and flexible library
// for robotics research.
//
// Copyright (C) 2011 The University of Sydney
//
// snark is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 3 of the License, or (at your option) any later version.
//
// snark is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
// for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with snark. If not, see <http://www.gnu.org/licenses/>.

#include <iostream>
#include <boost/array.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/serial_port.hpp>
#include <boost/bind.hpp>
#include <boost/optional.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/static_assert.hpp>
#include <comma/base/exception.h>
#include <comma/base/last_error.h>
#include <comma/io/select.h>
#include <comma/string/string.h>
#include "./commands.h"
#include "./packet.h"
#include "./protocol.h"

namespace snark { namespace quickset { namespace ptcr {

class transport // todo: quick and dirty; decouple later, once needed
{
    public:
        transport( const std::string& name )
        {
            std::vector< std::string > v = comma::split( name, ':' );
            if( v[0] == "tcp" )
            {
                if( v.size() != 3 ) { COMMA_THROW( comma::exception, "expected tcp:address:port, got " << name ); }
                boost::asio::ip::tcp::resolver resolver( service_ );
                boost::asio::ip::tcp::resolver::query query( v[1], v[2] );
                boost::asio::ip::tcp::resolver::iterator it = resolver.resolve( query );
                socket_.reset( new boost::asio::ip::tcp::socket( service_ ) );
                socket_->connect( it->endpoint() );
                socket_->set_option( boost::asio::ip::tcp::no_delay( true ) );
                if( !socket_->is_open() ) { COMMA_THROW( comma::exception, "failed to open socket " << name ); }
            }
            else
            {
                serial_.reset( new boost::asio::serial_port( service_, name ) );
                serial_->set_option( boost::asio::serial_port::baud_rate() ); // autobaud
                serial_->set_option( boost::asio::serial_port::parity( boost::asio::serial_port::parity::none ) );
                serial_->set_option( boost::asio::serial_port::stop_bits( boost::asio::serial_port::stop_bits::one ) );
                serial_->set_option( boost::asio::serial_port::character_size( 8 ) );
                if( !serial_->is_open() ) { COMMA_THROW( comma::exception, "failed to open serial port " << name ); }
            }
        }

        void close() { if( serial_ ) { serial_->close(); } else { socket_->close(); } };

        comma::io::file_descriptor fd() const { return serial_ ? serial_->native() : socket_->native(); }
        
    private:
        boost::asio::io_service service_;
        boost::scoped_ptr< boost::asio::serial_port > serial_;
        boost::scoped_ptr< boost::asio::ip::tcp::socket > socket_;
};

class protocol::impl
{
    public:
        impl( const std::string& name )
            : transport_( name )
        {
            select_.read().add( transport_ );
        }

        template < typename C >
        const packet< typename C::response >* send( const C& command )
        {
            if( command_pending ) { COMMA_THROW( comma::exception, "cannot send command 0x" << std::hex << ( 0xff & C::id ) << std::dec << " since command 0x" << std::hex << ( 0xff & *command_pending ) << std::dec << " is pending" ); }
            BOOST_STATIC_ASSERT( packet< C >::size < 64 );
            BOOST_STATIC_ASSERT( packet< typename C::response >::size < 64 );
            packet< C > p( command );
            escape_( p );
            send_();
            command_pending = C::id;
            bool ok = receive_();
            command_pending.reset();
            if( !ok ) { return NULL; }
            if( !unescape_< packet< typename C::response > >() ) { return NULL; }
            const packet< typename C::response >* r = reinterpret_cast< const packet< typename C::response >* >( &response_[0] );
            if( r->packet_header.id() != C::id ) { return NULL; } 
            if( r->packet_footer.etx() != constants::etx ) { return NULL; }
            if( !r->packet_footer.footer_lrc.ok() ) { return NULL; } 
            return r;
        }

        void close() { transport_.close(); }

    private:
        boost::optional< unsigned char > command_pending;
        transport transport_;
        boost::array< char, 128 > txbuf_; // quick and dirty: arbitrary size
        std::size_t txsize_;
        boost::array< char, 128 > rxbuf_; // quick and dirty: arbitrary size
        boost::array< char, 64 > response_; // quick and dirty: arbitrary size
        comma::io::select select_;

        static boost::array< bool, 256 > init_escaped()
        {
            boost::array< bool, 256 > e;
            for( unsigned int i = 0; i < e.size(); e[ i++ ] = false );
            e[ constants::ack ] = true;
            e[ constants::nak ] = true;
            e[ constants::stx ] = true;
            e[ constants::etx ] = true;
            e[ constants::esc ] = true;
            return e;
        }

        static boost::array< bool, 256 > escaped_;

        template < typename P >
        void escape_( const P& p )
        {
            txbuf_[0] = constants::stx;
            const char* begin = p.packet_header.address.data();
            const char* end = p.packet_footer.etx.data();
            char* cur = &txbuf_[1];
            for( const char* s = begin; s < end; ++s, ++cur )
            {
                if( escaped_[ static_cast< unsigned char >( *s ) ] )
                {
                    *cur++ = constants::esc;
                    *cur = ( *s ) | 0x80;
                }
                else
                {
                    *cur = *s;
                }
            }
            *cur++ = constants::etx;
            txsize_ = cur - &txbuf_[0];
        }

        template < typename P >
        bool unescape_()
        {
            if( rxbuf_[0] != constants::ack && rxbuf_[0] != constants::nak ) { return false; }
            char* cur = &response_[0];
            for( const char* s = &rxbuf_[0]; *s != constants::etx; ++s, ++cur )
            {
                *cur = *s == constants::esc ? *( ++s ) & 0x7f : *s;
                if( *s == constants::esc ) { std::cerr << std::hex << "--> unescaped " << ( 0xff & *s ) << std::dec << std::endl; }
            }
            *cur = constants::etx;
            return true;
        }

        void send_()
        {
            const char* begin = &txbuf_[0];
            const char* end = &txbuf_[0] + txsize_;
            unsigned int sent = 0;
            for( const char* t = begin; t < end; )
            {
                int s = ::write( transport_.fd(), t, txsize_ - sent );
                if( s < 0 ) { COMMA_THROW( comma::exception, "failed to send: " << comma::last_error::to_string() ); }
                t += s;
                sent += s;
            }
        }

        bool receive_() // todo: quick and dirty, reading 1 byte at time may be slow - watch
        {
            static const boost::posix_time::time_duration timeout = boost::posix_time::seconds( 1 ); // arbitrary
            for( char* cur = &rxbuf_[0]; select_.wait( timeout ) != 0; ++cur )
            {
                int r = ::read( transport_.fd(), cur, 1 );
                if( r < 0 ) { COMMA_THROW( comma::exception, "connection failed" ); }
                if( r == 0 ) { COMMA_THROW( comma::exception, "connection closed" ); }
                if( *cur == constants::etx ) { return true; } // quick and dirty, don't care for ack/nak byte for now
            }
            return false;
        }
};

boost::array< bool, 256 > protocol::impl::escaped_ = protocol::impl::init_escaped();

protocol::protocol( const std::string& name ) : pimpl_( new impl( name ) ) {}

protocol::~protocol() { close(); delete pimpl_; }

void protocol::close() { pimpl_->close(); }

template < typename C >
const packet< typename C::response >* protocol::send( const C& command ) { return pimpl_->send( command ); }

template const packet< commands::get_status::response >* protocol::send< commands::get_status >( const commands::get_status& );
template const packet< commands::move_to::response >* protocol::send< commands::move_to >( const commands::move_to& );
template const packet< commands::move_to_delta::response >* protocol::send< commands::move_to_delta >( const commands::move_to_delta& );
template const packet< commands::get_limits::response >* protocol::send< commands::get_limits >( const commands::get_limits& );
template const packet< commands::set_limits::response >* protocol::send< commands::set_limits >( const commands::set_limits& );
template const packet< commands::set_camera::response >* protocol::send< commands::set_camera >( const commands::set_camera& );

} } } // namespace snark { namespace quickset { namespace ptcr {
    