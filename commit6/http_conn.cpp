#include "http_conn.h"
#include "log.h"


const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";
const char* doc_root = "./htdocs";

int setnonblocking( int fd )
{
	int old_option = fcntl( fd, F_GETFL );
	int new_option = old_option | O_NONBLOCK;
	fcntl( fd, F_SETFL, new_option );
	return old_option;
}

void addfd( int epollfd, int fd, bool one_shot )
{
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
	if( one_shot )
	{
		event.events |= EPOLLONESHOT;
	}
	epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
	setnonblocking( fd );
}

void removefd( int epollfd, int fd )
{
	epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0 );
	close( fd );
}

void modfd( int epollfd, int fd, int ev )
{
	epoll_event event;
	event.data.fd = fd;
	event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
	epoll_ctl( epollfd, EPOLL_CTL_MOD, fd, &event );
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

void http_conn::close_conn( bool real_close )
{
	if( real_close && ( m_sockfd != -1 ) )
	{
		//modfd( m_epollfd, m_sockfd, EPOLLIN );
		removefd( m_epollfd, m_sockfd );
		m_sockfd = -1;
		m_user_count--;
	}
}

void http_conn::init( int sockfd, const sockaddr_in& addr )
{
	m_sockfd = sockfd;
	m_address = addr;
	//int error = 0;
	//socklen_t len = sizeof( error );
	//getsockopt( m_sockfd, SOL_SOCKET, SO_ERROR, &error, &len );
	int reuse = 1;
	setsockopt( m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
	addfd( m_epollfd, sockfd, true );
	m_user_count++;

	init();
}

void http_conn::init()
{
	m_check_state = CHECK_STATE_REQUESTLINE;
	m_linger = false;

	m_method = GET;
	m_url = 0;
	m_version = 0;
	m_content_length = 0;
	m_host = 0;
	m_start_line = 0;
	m_checked_idx = 0;
	m_read_idx = 0;
	m_write_idx = 0;
	cgi=0;
	memset( m_read_buf, '\0', READ_BUFFER_SIZE );
	memset( m_write_buf, '\0', WRITE_BUFFER_SIZE );
	memset( m_real_file, '\0', FILENAME_LEN );
}

http_conn::LINE_STATUS http_conn::parse_line()
{
	char temp;
	for ( ; m_checked_idx < m_read_idx; ++m_checked_idx )
	{
		temp = m_read_buf[ m_checked_idx ];
		if ( temp == '\r' )
		{
			if ( ( m_checked_idx + 1 ) == m_read_idx )
			{
				return LINE_OPEN;
			}
			else if ( m_read_buf[ m_checked_idx + 1 ] == '\n' )
			{
				m_read_buf[ m_checked_idx++ ] = '\0';
				m_read_buf[ m_checked_idx++ ] = '\0';
				return LINE_OK;
			}

			return LINE_BAD;
		}
		else if( temp == '\n' )
		{
			if( ( m_checked_idx > 1 ) && ( m_read_buf[ m_checked_idx - 1 ] == '\r' ) )
			{
				m_read_buf[ m_checked_idx-1 ] = '\0';
				m_read_buf[ m_checked_idx++ ] = '\0';
				return LINE_OK;
			}
			return LINE_BAD;
		}
	}

	return LINE_OPEN;
}

//bool http_conn::read()
bool http_conn::read_1()
{
	if( m_read_idx >= READ_BUFFER_SIZE )
	{
		return false;
	}

	int bytes_read = 0;
	while( true )
	{
		bytes_read = recv( m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0 );
		if ( bytes_read == -1 )
		{
			if( errno == EAGAIN || errno == EWOULDBLOCK )
			{
				break;
			}
			return false;
		}
		else if ( bytes_read == 0 )
		{
			return false;
		}

		m_read_idx += bytes_read;
	}
	return true;
}

http_conn::HTTP_CODE http_conn::parse_request_line( char* text )
{
	m_url = strpbrk( text, " \t" );
	if ( ! m_url )
	{
		return BAD_REQUEST;
	}
	*m_url++ = '\0';

	char* method = text;
	if ( strcasecmp( method, "GET" ) == 0 )
	{
		m_method = GET;
	}
	else if(strcasecmp(method,"POST")==0)
	{
		m_method=POST;
		cgi=1;
	}
	else
	{
		return BAD_REQUEST;
	}

	m_url += strspn( m_url, " \t" );
	m_version = strpbrk( m_url, " \t" );
	if ( ! m_version )
	{
		return BAD_REQUEST;
	}
	*m_version++ = '\0';
	m_version += strspn( m_version, " \t" );
	if ( strcasecmp( m_version, "HTTP/1.1" ) != 0 )
	{
		return BAD_REQUEST;
	}

	if ( strncasecmp( m_url, "http://", 7 ) == 0 )
	{
		m_url += 7;
		m_url = strchr( m_url, '/' );
	}

	if ( ! m_url || m_url[ 0 ] != '/' )
	{
		return BAD_REQUEST;
	}
	if(strlen(m_url)==1)
	{
		strcat(m_url,"index.html");
	}

	m_check_state = CHECK_STATE_HEADER;
	return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_headers( char* text )
{
	if( text[ 0 ] == '\0' )
	{
		if ( m_method == HEAD )
		{
			return GET_REQUEST;
		}

		if ( m_content_length != 0 )
		{
			m_check_state = CHECK_STATE_CONTENT;
			return NO_REQUEST;
		}

		return GET_REQUEST;
	}
	else if ( strncasecmp( text, "Connection:", 11 ) == 0 )
	{
		text += 11;
		text += strspn( text, " \t" );
		if ( strcasecmp( text, "keep-alive" ) == 0 )
		{
			m_linger = true;
		}
	}
	else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 )
	{
		text += 15;
		text += strspn( text, " \t" );
		m_content_length = atol( text );
	}
	else if ( strncasecmp( text, "Host:", 5 ) == 0 )
	{
		text += 5;
		text += strspn( text, " \t" );
		m_host = text;
	}
	else
	{
		//printf( "oop! unknow header %s\n", text );
		LOG_INFO("oop!unknow header: %s\n",text);
		Log::get_instance()->flush();
		return NO_REQUEST;//
	}

}

http_conn::HTTP_CODE http_conn::parse_content( char* text )
{
	if ( m_read_idx >= ( m_content_length + m_checked_idx ) )
	{
		text[ m_content_length ] = '\0';
		m_string=text;
		return GET_REQUEST;
	}

	return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read()
{
	LINE_STATUS line_status = LINE_OK;
	HTTP_CODE ret = NO_REQUEST;
	char* text = 0;

	while ( ( ( m_check_state == CHECK_STATE_CONTENT ) && ( line_status == LINE_OK  ) )
			|| ( ( line_status = parse_line() ) == LINE_OK ) )
	{
		text = get_line();
		m_start_line = m_checked_idx;
		//printf( "got 1 http line: %s\n", text );
		LOG_INFO("got 1 http line:%s",text);
		Log::get_instance()->flush();

		switch ( m_check_state )
		{
			case CHECK_STATE_REQUESTLINE:
				{
					ret = parse_request_line( text );
					if ( ret == BAD_REQUEST )
					{
						return BAD_REQUEST;
					}
					break;
				}
			case CHECK_STATE_HEADER:
				{
					ret = parse_headers( text );
					if ( ret == BAD_REQUEST )
					{
						return BAD_REQUEST;
					}
					else if ( ret == GET_REQUEST )
					{
						return do_request();
					}
					break;
				}
			case CHECK_STATE_CONTENT:
				{
					ret = parse_content( text );
					if ( ret == GET_REQUEST )
					{
						return do_request();
					}
					line_status = LINE_OPEN;
					break;
				}
			default:
				{
					return INTERNAL_ERROR;
				}
		}
	}

	return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()
{
	strcpy( m_real_file, doc_root );
	int len = strlen( doc_root );
	//
	if(cgi==1)
	{
		strncpy(m_real_file+len,m_url,FILENAME_LEN-len-1);
		printf("%s\n",m_real_file);
		char name[100],password[100];
		int i;
		for(i=5;m_string[i]!='&';++i)
			name[i-5]=m_string[i];
		name[i-5]='\0';
		int j=0;
		for(i=i+10;m_string[i]!='\0';++i,++j)
			password[j]=m_string[i];
		password[j]='\0';

		pid_t pid;
		int pipefd[2];
		if(pipe(pipefd)<0)
		{
			LOG_ERROR("pipe() error:%d",4);
			return BAD_REQUEST;
		}

		if((pid=fork())<0)
		{
			LOG_ERROR("fork() error:%d",3);
			return BAD_REQUEST;
		}
		if(pid==0)
		{
			//dup2(pipefd[1],1);
			close(pipefd[0]);
			close(STDOUT_FILENO);
			dup(pipefd[1]);
			execl(m_real_file,m_real_file,name,password,NULL);
		}
		else
		{
			close(pipefd[1]);
			char result;
			int ret=read(pipefd[0],&result,1);
			if(ret!=1)
			{
				LOG_ERROR("read error:ret=%d",ret);
				return BAD_REQUEST;
			}
			if(result=='1')
				m_url="/welcome.html";
			else
				m_url="/error.html";
			waitpid(pid,0,NULL);
		}	
	}
	//
	strncpy( m_real_file + len, m_url, FILENAME_LEN - len - 1 );
	//
	LOG_INFO("the request file:%s",m_real_file);
	Log::get_instance()->flush();
	//
	if ( stat( m_real_file, &m_file_stat ) < 0 )
	{
		return NO_RESOURCE;
	}

	if ( ! ( m_file_stat.st_mode & S_IROTH ) )
	{
		return FORBIDDEN_REQUEST;
	}

	if ( S_ISDIR( m_file_stat.st_mode ) )
	{
		return BAD_REQUEST;
	}

	int fd = open( m_real_file, O_RDONLY );
	m_file_address = ( char* )mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
	close( fd );
	return FILE_REQUEST;
}

void http_conn::unmap()
{
	if( m_file_address )
	{
		munmap( m_file_address, m_file_stat.st_size );
		m_file_address = 0;
	}
}

bool http_conn::write()
{
	int temp = 0;
	int bytes_have_send = 0;
	int bytes_to_send = m_write_idx;
	if ( bytes_to_send == 0 )
	{
		modfd( m_epollfd, m_sockfd, EPOLLIN );
		init();
		return true;
	}

	while( 1 )
	{
		temp = writev( m_sockfd, m_iv, m_iv_count );
		// printf("temp:%d\n",temp);
		LOG_INFO("the length has been send:%d",temp);
		Log::get_instance()->flush();
		if ( temp <= -1 )
		{
			if( errno == EAGAIN )
			{
				modfd( m_epollfd, m_sockfd, EPOLLOUT );
				return true;
			}
			unmap();
			return false;
		}

		bytes_to_send -= temp;
		bytes_have_send += temp;
		if ( bytes_to_send <= bytes_have_send )
		{
			unmap();
			if( m_linger )
			{
				init();
				modfd( m_epollfd, m_sockfd, EPOLLIN );
				return true;
			}
			else
			{
				modfd( m_epollfd, m_sockfd, EPOLLIN );
				return false;
			} 
		}
	}
}

bool http_conn::add_response( const char* format, ... )
{
	if( m_write_idx >= WRITE_BUFFER_SIZE )
	{
		return false;
	}
	va_list arg_list;
	va_start( arg_list, format );
	int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );
	if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) )
	{
		return false;
	}
	m_write_idx += len;
	va_end( arg_list );
	//
	//printf("%s\n",m_write_buf);
	LOG_INFO("the Server response:%s",m_write_buf);
	Log::get_instance()->flush();
	//
	return true;
}

bool http_conn::add_status_line( int status, const char* title )
{
	return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

bool http_conn::add_headers( int content_len )
{
	add_content_length( content_len );
	add_linger();
	add_blank_line();
}

bool http_conn::add_content_length( int content_len )
{
	return add_response( "Content-Length: %d\r\n", content_len );
}

bool http_conn::add_linger()
{
	return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

bool http_conn::add_blank_line()
{
	return add_response( "%s", "\r\n" );
}

bool http_conn::add_content( const char* content )
{
	return add_response( "%s", content );
}

bool http_conn::process_write( HTTP_CODE ret )
{
	switch ( ret )
	{
		case INTERNAL_ERROR:
			{
				add_status_line( 500, error_500_title );
				add_headers( strlen( error_500_form ) );
				if ( ! add_content( error_500_form ) )
				{
					return false;
				}
				break;
			}
		case BAD_REQUEST:
			{
				add_status_line( 400, error_400_title );
				add_headers( strlen( error_400_form ) );
				if ( ! add_content( error_400_form ) )
				{
					return false;
				}
				break;
			}
		case NO_RESOURCE:
			{
				add_status_line( 404, error_404_title );
				add_headers( strlen( error_404_form ) );
				if ( ! add_content( error_404_form ) )
				{
					return false;
				}
				break;
			}
		case FORBIDDEN_REQUEST:
			{
				add_status_line( 403, error_403_title );
				add_headers( strlen( error_403_form ) );
				if ( ! add_content( error_403_form ) )
				{
					return false;
				}
				break;
			}
		case FILE_REQUEST:
			{
				add_status_line( 200, ok_200_title );
				if ( m_file_stat.st_size != 0 )
				{
					add_headers( m_file_stat.st_size );
					m_iv[ 0 ].iov_base = m_write_buf;
					m_iv[ 0 ].iov_len = m_write_idx;
					m_iv[ 1 ].iov_base = m_file_address;
					m_iv[ 1 ].iov_len = m_file_stat.st_size;
					m_iv_count = 2;
					return true;
				}
				else
				{
					const char* ok_string = "<html><body></body></html>";
					add_headers( strlen( ok_string ) );
					if ( ! add_content( ok_string ) )
					{
						return false;
					}
				}
			}
		default:
			{
				return false;
			}
	}

	m_iv[ 0 ].iov_base = m_write_buf;
	m_iv[ 0 ].iov_len = m_write_idx;
	m_iv_count = 1;
	return true;
}

void http_conn::process()
{
	HTTP_CODE read_ret = process_read();
	if ( read_ret == NO_REQUEST )
	{
		modfd( m_epollfd, m_sockfd, EPOLLIN );
		return;
	}

	bool write_ret = process_write( read_ret );
	if ( ! write_ret )
	{
		close_conn();
	}

	modfd( m_epollfd, m_sockfd, EPOLLOUT );
}


