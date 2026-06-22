/* Copyright 2012 William Woodall and John Harrison */
#include <algorithm>

#if !defined( _WIN32 ) && !defined( __OpenBSD__ ) && !defined( __FreeBSD__ )
  #include <alloca.h>
#endif

#if defined( __MINGW32__ ) && !defined( alloca )
  #define alloca __builtin_alloca
#endif

#include "serial_cpp/serial.h"

#ifdef _WIN32
  #include "serial_cpp/impl/win.h"
#else
  #include "serial_cpp/impl/unix.h"
#endif

using serial_cpp::bytesize_t;
using serial_cpp::flowcontrol_t;
using serial_cpp::IOException;
using serial_cpp::parity_t;
using serial_cpp::Serial;
using serial_cpp::SerialException;
using serial_cpp::stopbits_t;

Serial::Serial( const std::string_view port, std::uint32_t baudrate, const serial_cpp::Timeout& timeout, bytesize_t bytesize, parity_t parity, stopbits_t stopbits, flowcontrol_t flowcontrol ) :
  pimpl_( new SerialImpl( port, baudrate, bytesize, parity, stopbits, flowcontrol ) )
{ pimpl_->setTimeout( timeout ); }

Serial& Serial::operator=( Serial&& other ) noexcept
{
  if( this != &other )
  {
    std::lock_guard<std::mutex> lock_read( m_read );
    std::lock_guard<std::mutex> lock_write( m_write );

    pimpl_ = std::move( other.pimpl_ );
  }
  return *this;
}

Serial::Serial( Serial&& other ) noexcept : pimpl_( std::move( other.pimpl_ ) ) {}

Serial::~Serial() noexcept {}

void Serial::open() { pimpl_->open(); }

void Serial::close() { pimpl_->close(); }

bool Serial::isOpen() const { return pimpl_->isOpen(); }

std::size_t Serial::available() { return pimpl_->available(); }

bool Serial::waitReadable()
{
  serial_cpp::Timeout timeout( pimpl_->getTimeout() );
  return pimpl_->waitReadable( timeout.read_timeout_constant );
}

void Serial::waitByteTimes( std::size_t count ) { pimpl_->waitByteTimes( count ); }

std::size_t Serial::read_( std::uint8_t* buffer, std::size_t size ) { return this->pimpl_->read( buffer, size ); }

std::size_t Serial::read( std::uint8_t* buffer, std::size_t size )
{
  std::lock_guard<std::mutex> lock( m_read );
  return this->pimpl_->read( buffer, size );
}

std::size_t Serial::read( std::vector<std::uint8_t>& buffer, std::size_t size )
{
  std::lock_guard<std::mutex> lock( m_read );
  std::uint8_t*               buffer_    = new std::uint8_t[size];
  std::size_t                 bytes_read = 0;

  try
  {
    bytes_read = this->pimpl_->read( buffer_, size );
  }
  catch( const std::exception& e )
  {
    delete[] buffer_;
    throw;
  }

  buffer.insert( buffer.end(), buffer_, buffer_ + bytes_read );
  delete[] buffer_;
  return bytes_read;
}

std::size_t Serial::read( std::string& buffer, std::size_t size )
{
  std::lock_guard<std::mutex> lock( m_read );
  std::uint8_t*               buffer_    = new std::uint8_t[size];
  std::size_t                 bytes_read = 0;
  try
  {
    bytes_read = this->pimpl_->read( buffer_, size );
  }
  catch( const std::exception& e )
  {
    delete[] buffer_;
    throw;
  }
  buffer.append( reinterpret_cast<const char*>( buffer_ ), bytes_read );
  delete[] buffer_;
  return bytes_read;
}

std::string Serial::read( std::size_t size )
{
  std::string buffer;
  this->read( buffer, size );
  return buffer;
}

std::size_t Serial::readline( std::string& buffer, std::size_t size, std::string eol )
{
  std::lock_guard<std::mutex> lock( m_read );
  std::size_t                 eol_len     = eol.length();
  std::uint8_t*               buffer_     = static_cast<std::uint8_t*>( alloca( size * sizeof( std::uint8_t ) ) );
  std::size_t                 read_so_far = 0;
  while( true )
  {
    std::size_t bytes_read = this->read_( buffer_ + read_so_far, 1 );
    read_so_far += bytes_read;
    if( bytes_read == 0 )
    {
      break;  // Timeout occurred on reading 1 byte
    }
    if( read_so_far < eol_len ) continue;
    if( std::string( reinterpret_cast<const char*>( buffer_ + read_so_far - eol_len ), eol_len ) == eol )
    {
      break;  // EOL found
    }
    if( read_so_far == size )
    {
      break;  // Reached the maximum read length
    }
  }
  buffer.append( reinterpret_cast<const char*>( buffer_ ), read_so_far );
  return read_so_far;
}

std::string Serial::readline( std::size_t size, std::string eol )
{
  std::string buffer;
  this->readline( buffer, size, eol );
  return buffer;
}

std::vector<std::string> Serial::readlines( std::size_t size, std::string eol )
{
  std::lock_guard<std::mutex> lock( m_read );
  std::vector<std::string>    lines;
  std::size_t                 eol_len       = eol.length();
  std::uint8_t*               buffer_       = static_cast<std::uint8_t*>( alloca( size * sizeof( std::uint8_t ) ) );
  std::size_t                 read_so_far   = 0;
  std::size_t                 start_of_line = 0;
  while( read_so_far < size )
  {
    std::size_t bytes_read = this->read_( buffer_ + read_so_far, 1 );
    read_so_far += bytes_read;
    if( bytes_read == 0 )
    {
      if( start_of_line != read_so_far ) { lines.push_back( std::string( reinterpret_cast<const char*>( buffer_ + start_of_line ), read_so_far - start_of_line ) ); }
      break;  // Timeout occurred on reading 1 byte
    }
    if( read_so_far < eol_len ) continue;
    if( std::string( reinterpret_cast<const char*>( buffer_ + read_so_far - eol_len ), eol_len ) == eol )
    {
      // EOL found
      lines.push_back( std::string( reinterpret_cast<const char*>( buffer_ + start_of_line ), read_so_far - start_of_line ) );
      start_of_line = read_so_far;
    }
    if( read_so_far == size )
    {
      if( start_of_line != read_so_far ) { lines.push_back( std::string( reinterpret_cast<const char*>( buffer_ + start_of_line ), read_so_far - start_of_line ) ); }
      break;  // Reached the maximum read length
    }
  }
  return lines;
}

std::size_t Serial::write( const std::string& data )
{
  std::lock_guard<std::mutex> lock( m_write );
  return this->write_( reinterpret_cast<const uint8_t*>( data.c_str() ), data.length() );
}

std::size_t Serial::write( const std::vector<std::uint8_t>& data )
{
  std::lock_guard<std::mutex> lock( m_write );
  return this->write_( &data[0], data.size() );
}

std::size_t Serial::write( const std::uint8_t* data, std::size_t size )
{
  std::lock_guard<std::mutex> lock( m_write );
  return this->write_( data, size );
}

std::size_t Serial::write_( const std::uint8_t* data, std::size_t length ) { return pimpl_->write( data, length ); }

void Serial::setPort( const std::string& port )
{
  std::lock_guard<std::mutex> lock( m_read );
  std::lock_guard<std::mutex> lock_write( m_write );
  bool                        was_open = pimpl_->isOpen();
  if( was_open ) close();
  pimpl_->setPort( port );
  if( was_open ) open();
}

std::string Serial::getPort() const { return pimpl_->getPort(); }

void Serial::setTimeout( const serial_cpp::Timeout& timeout ) { pimpl_->setTimeout( timeout ); }

serial_cpp::Timeout Serial::getTimeout() const { return pimpl_->getTimeout(); }

void Serial::setBaudrate( uint32_t baudrate ) { pimpl_->setBaudrate( baudrate ); }

uint32_t Serial::getBaudrate() const { return uint32_t( pimpl_->getBaudrate() ); }

void Serial::setBytesize( bytesize_t bytesize ) { pimpl_->setBytesize( bytesize ); }

bytesize_t Serial::getBytesize() const { return pimpl_->getBytesize(); }

void Serial::setParity( parity_t parity ) { pimpl_->setParity( parity ); }

parity_t Serial::getParity() const { return pimpl_->getParity(); }

void Serial::setStopbits( stopbits_t stopbits ) { pimpl_->setStopbits( stopbits ); }

stopbits_t Serial::getStopbits() const { return pimpl_->getStopbits(); }

void Serial::setFlowcontrol( flowcontrol_t flowcontrol ) { pimpl_->setFlowcontrol( flowcontrol ); }

flowcontrol_t Serial::getFlowcontrol() const { return pimpl_->getFlowcontrol(); }

void Serial::flush()
{
  std::lock_guard<std::mutex> lock( m_read );
  std::lock_guard<std::mutex> lock_write( m_write );
  pimpl_->flush();
}

void Serial::flushInput()
{
  std::lock_guard<std::mutex> lock( m_read );
  pimpl_->flushInput();
}

void Serial::flushOutput()
{
  std::lock_guard<std::mutex> lock( m_write );
  pimpl_->flushOutput();
}

void Serial::sendBreak( int duration ) { pimpl_->sendBreak( duration ); }

void Serial::setBreak( bool level ) { pimpl_->setBreak( level ); }

void Serial::setRTS( bool level ) { pimpl_->setRTS( level ); }

void Serial::setDTR( bool level ) { pimpl_->setDTR( level ); }

bool Serial::waitForChange() { return pimpl_->waitForChange(); }

bool Serial::getCTS() { return pimpl_->getCTS(); }

bool Serial::getDSR() { return pimpl_->getDSR(); }

bool Serial::getRI() { return pimpl_->getRI(); }

bool Serial::getCD() { return pimpl_->getCD(); }
