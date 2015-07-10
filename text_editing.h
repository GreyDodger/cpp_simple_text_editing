// TODO:
// ====
//	
//
//	write test code
//	duplicate line???
//	
//
//	Features
//	========
//		- visual select (or selecting multiple characters at once)
//		- regex
//		- queries? find out number of searches, get number of text characters
//      - registers / copying / pasting 
//		- unicode support
//			+ only requires changing how the text is read, don't need to change
//			the insert function
//		- funcitons for inserting multiple strings, or removing multiple
//		sections
//
//	Lower Priority Features
//	=======================
//		- marks
//		- undo / redo
//		- autoformatting code
//
//	Code Cleanup
//	============
//		- maybe there should be a different name for in_index, too similar to
//		index
//		- resize() should be able to resize down
//
//  Speedup Ideas
//	=============
//		- keep track in data of the first active chunk, and the index to the
//		first and last chunk
//		
//  Reduce Fragmentation Ideas
//	==========================
//


#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace text_editing
{

enum class visual_selection_mode
{
	normal, block,
};

char* to_string( visual_selection_mode vsm )
{
	switch( vsm )
	{
		case visual_selection_mode::normal: return "normal";
		case visual_selection_mode::block: return "block";
	}
	return nullptr;
}

#define TEXT_CHUNK_SIZE 4

struct text_chunk_data
{
	int len = 0;
	int index_next = -1;
	int index_previous = -1;
};

#define CHK_DATA(index)		te->tc_data[ index ]
#define CHK_MEM(index)		te->text_chunks + ( TEXT_CHUNK_SIZE * index )

struct te_state
{
	char file_name[256];

	union 
	{
		void * text_mem = nullptr;

		struct
		{
			char * text_chunks;
			text_chunk_data * tc_data;
		};
	};

	int num_active_chunks			= 0;

	int cursor_position				= 0;

	visual_selection_mode vs_mode	= visual_selection_mode::normal;
	int selection_begin				= 0;
	int selection_end				= 0;

	visual_selection_mode vs_mode_prev	= visual_selection_mode::normal;
	int selection_begin_prev			= 0;
	int selection_end_prev				= 0;

	int num_text_chunks				= 0;

	// last command, for undoing and repeating

	// register contents

	te_state() 
	{
		memset( file_name, 0, 256 );
	}
};


namespace _internal // {{{
{

#define LOOP(count, var_name) for(int var_name = 0; var_name < count; var_name++ )
#define LOOP_PARTIAL(count, var_name, start_val ) for(int var_name = start_val; var_name < count; var_name++ )
#define LOOP_BACKWARDS(count, var_name) for(int var_name = count - 1; var_name >= 0; var_name-- )
#define LOOP_COMBINATIONS(count, outer_var_name, var_name) \
	for(int var_name = outer_var_name + 1; var_name < count; var_name++ )

#define INFINITE_LOOP while( 1 )
#define ABSTRACT_METHOD(x) virtual x = 0

#define MAX(a,b) (a>b?a:b)
#define MIN(a,b) (a<b?a:b)

int get_file_size( FILE * file )
{
	auto current_pos = ftell( file );
	fseek( file, 0L, SEEK_END );
	auto result = ftell( file );
	fseek( file, current_pos, SEEK_SET ); 
	return result;
}




void mem_resize( te_state * te, int num_text_chunks )
{
	int new_text_mem_size = ( TEXT_CHUNK_SIZE * num_text_chunks ) + 
		( sizeof( text_chunk_data ) * num_text_chunks );
	int old_text_mem_size = ( TEXT_CHUNK_SIZE * te->num_text_chunks ) + 
		( sizeof( text_chunk_data ) * te->num_text_chunks );

	void* new_text_mem = malloc( new_text_mem_size );
	memset( new_text_mem, 0, new_text_mem_size );

	// init the text chunk data
	text_chunk_data * old_tc_data = 
		(text_chunk_data*)(((char*)te->text_mem) + ( TEXT_CHUNK_SIZE * te->num_text_chunks ));
	text_chunk_data * new_tc_data = 
		(text_chunk_data*)(((char*)new_text_mem) + ( TEXT_CHUNK_SIZE * num_text_chunks ));
	LOOP( num_text_chunks, i )
	{
		new_tc_data[i].index_next = -1;
		new_tc_data[i].index_previous = -1;
	}


	if( te->text_mem )
	{
		memmove( new_text_mem, te->text_mem, TEXT_CHUNK_SIZE * te->num_text_chunks );
		memmove( new_tc_data, old_tc_data, sizeof( text_chunk_data ) * te->num_text_chunks );
		free( te->text_mem );
	}

	te->text_mem = new_text_mem;
	te->num_text_chunks = num_text_chunks;

	te->text_chunks = (char*)te->text_mem;
	te->tc_data = (text_chunk_data*)(((char*)te->text_mem) + ( TEXT_CHUNK_SIZE * num_text_chunks ));
}


int get_index_of_first_text_chunk( te_state * te );

void log_te_state( char * log_buffer, te_state * te )
{
	//sprintf( log_buffer + strlen( log_buffer ), "LOGGING VIM STATE:" );
	//sprintf( log_buffer + strlen( log_buffer ), "\nfile name: %s", te->file_name );
	//sprintf( log_buffer + strlen( log_buffer ), "\ncursor position: %i", te->cursor_position );
	//sprintf( log_buffer + strlen( log_buffer ), "\nvisual selection mode: %s", to_string( te->vs_mode ) );
	//sprintf( log_buffer + strlen( log_buffer ), "\nvisual selection begin: %i", te->selection_begin );
	//sprintf( log_buffer + strlen( log_buffer ), "\nvisual selection end: %i", te->selection_end );

	//sprintf( log_buffer + strlen( log_buffer ), "\ntext chunk size: %i", TEXT_CHUNK_SIZE );
	//sprintf( log_buffer + strlen( log_buffer ), "\ntext chunk count: %i", NUM_TEXT_CHUNKS );
	// NOTE removed \n
	sprintf( log_buffer + strlen( log_buffer ), "TEXT CHUNKS" );

	int active_chunks = te->num_active_chunks;

	LOOP( active_chunks, i )
	{
		if( CHK_DATA( i ).len == 0 && 
			CHK_DATA( i ).index_next == -1 &&
			CHK_DATA( i ).index_previous == -1 )
		{
			active_chunks++;
		}
		else
		{
			sprintf( log_buffer + strlen( log_buffer ), "\n%i )", i );
			sprintf( log_buffer + strlen( log_buffer ), "\n -- text -- \n" );
			_snprintf( log_buffer + strlen( log_buffer ), TEXT_CHUNK_SIZE, 
				te->text_chunks + ( TEXT_CHUNK_SIZE * i ) );
			sprintf( log_buffer + strlen( log_buffer ), 
				"\n-- len: %i\n-- index previous: %i\n-- index next: %i", 
				te->tc_data[i].len, te->tc_data[i].index_previous, te->tc_data[i].index_next );
			sprintf( log_buffer + strlen( log_buffer ), "\n------------------" );
		}
	}

	sprintf( log_buffer + strlen( log_buffer ), "\n\n" );


	int on_chunk = get_index_of_first_text_chunk(te);

	do
	{
		_snprintf( log_buffer + strlen( log_buffer ), te->tc_data[ on_chunk ].len, 
			te->text_chunks + (TEXT_CHUNK_SIZE * on_chunk ) );
		on_chunk = te->tc_data[ on_chunk ].index_next;
	}
	while( on_chunk != -1 );
}

int get_first_active_chunk( te_state * te )
{
	if( te->num_active_chunks == 0 ) return -1;
	
	int looking_at_index = 0;
	INFINITE_LOOP
	{
		if( !( CHK_DATA( looking_at_index ).len == 0 && 
			CHK_DATA( looking_at_index ).index_next == -1 &&
			CHK_DATA( looking_at_index ).index_previous == -1 ) )
			return looking_at_index;
		looking_at_index++;
	}

	return -1;
}


int get_first_inactive_chunk( te_state * te, int * excluding_chunks = nullptr, int excluding_chunks_len = 0)
{
	if( te->num_active_chunks == 0 ) return 0;
	
	int looking_at_index = 0;
	INFINITE_LOOP
	{
		if( CHK_DATA( looking_at_index ).len == 0 && 
			CHK_DATA( looking_at_index ).index_next == -1 &&
			CHK_DATA( looking_at_index ).index_previous == -1 )
		{
			bool exlude_chunk = false;
			LOOP( excluding_chunks_len, i )
			{
				if( excluding_chunks[i] == looking_at_index )
					exlude_chunk = true;
			}
			if( !exlude_chunk ) return looking_at_index;
		}
		looking_at_index++;
	}

	return -1;
}

int get_index_of_first_text_chunk( te_state * te )
{
	int looking_at_index = get_first_active_chunk( te );
	if( looking_at_index == -1 ) return -1;

	INFINITE_LOOP
	{
		if( te->tc_data[ looking_at_index ].index_previous == -1 )
			return looking_at_index;
		looking_at_index = te->tc_data[ looking_at_index ].index_previous;
	}
}
int get_index_of_last_text_chunk( te_state * te )
{
	int looking_at_index = 0;
	INFINITE_LOOP
	{
		if( CHK_DATA( looking_at_index ).index_next == -1 )
			return looking_at_index;
		looking_at_index = CHK_DATA( looking_at_index ).index_next;
	}
}
int get_index_of_first_empty_chunk( te_state * te )
{
	int looking_at_index = 0;
	INFINITE_LOOP
	{
		if( CHK_DATA( looking_at_index ).index_next == -1 &&
			CHK_DATA( looking_at_index ).index_previous == -1 &&
			CHK_DATA( looking_at_index ).len == 0 )
			return looking_at_index;
		looking_at_index++;
	}
}





inline void link_chunks( te_state * te, int chunk_index_from, int chunk_index_to )
{
	if( chunk_index_from != -1 )
		CHK_DATA( chunk_index_from ).index_next = chunk_index_to;
	if( chunk_index_to != -1 )
		CHK_DATA( chunk_index_to ).index_previous = chunk_index_from;
}





void insert_at_chunk( te_state * te, char * text, int text_len, int chunk_index, int in_chunk_index )
{
	if( CHK_DATA( chunk_index ).len == 0 )
		te->num_active_chunks++;

	int num_ori_chars_to_push = CHK_DATA( chunk_index ).len - in_chunk_index;
	int ori_len = num_ori_chars_to_push;
	int num_new_chars_to_push = text_len;

	char original_chars_to_push[ TEXT_CHUNK_SIZE ] = { 0 };
	memmove( original_chars_to_push, CHK_MEM( chunk_index ) + in_chunk_index, 
		num_ori_chars_to_push );

	// first chunk
	{
		int num_text_chars_in_first_chunk = MIN( num_new_chars_to_push,
			TEXT_CHUNK_SIZE - in_chunk_index );
		memmove( CHK_MEM( chunk_index ) + in_chunk_index, text, 
			num_text_chars_in_first_chunk );
		num_new_chars_to_push -= num_text_chars_in_first_chunk;

		int num_ori_chars_in_first_chunk = MIN( num_ori_chars_to_push,
			TEXT_CHUNK_SIZE - in_chunk_index - num_text_chars_in_first_chunk );
		memmove( CHK_MEM( chunk_index ) + in_chunk_index +
			num_text_chars_in_first_chunk, original_chars_to_push,
			num_ori_chars_in_first_chunk );
		num_ori_chars_to_push -= num_ori_chars_in_first_chunk;

		CHK_DATA( chunk_index ).len = MIN( TEXT_CHUNK_SIZE, CHK_DATA( chunk_index ).len + 
			text_len + num_ori_chars_to_push );
	}


	int first_chunk_next_index = CHK_DATA( chunk_index ).index_next;
	int current_index = chunk_index;

	while( num_new_chars_to_push > 0 || num_ori_chars_to_push > 0 )
	{
		int new_chk_idx = -1;

		bool adding_new_chunk = CHK_DATA( CHK_DATA( current_index ).index_next
			).len == TEXT_CHUNK_SIZE;
		if( adding_new_chunk )
		{
			if( te->num_active_chunks + 1 >= te->num_text_chunks )
				mem_resize( te, te->num_text_chunks * 2 );
			te->num_active_chunks++;
			new_chk_idx = get_first_inactive_chunk( te, &chunk_index, 1 );
		}
		else
			new_chk_idx = CHK_DATA( current_index ).index_next;

		{
			int space_in_current_chunk = TEXT_CHUNK_SIZE - CHK_DATA( new_chk_idx ).len;

			int new_chars_pushed = text_len - num_new_chars_to_push;
			int ori_chars_pushed = ori_len - num_ori_chars_to_push;

			int text_chars_in_current_chunk = MIN( num_new_chars_to_push,
				space_in_current_chunk );
			int ori_chars_in_current_chunk = MIN( num_ori_chars_to_push, 
				space_in_current_chunk - text_chars_in_current_chunk );
			int num_chars_pushing = text_chars_in_current_chunk +
				ori_chars_in_current_chunk;
			
			memmove( CHK_MEM( new_chk_idx ) + num_chars_pushing, CHK_MEM(
					new_chk_idx ), CHK_DATA( new_chk_idx ).len ); 

			memmove( CHK_MEM( new_chk_idx ), text + new_chars_pushed,
				text_chars_in_current_chunk ); 
			memmove( CHK_MEM( new_chk_idx ) + text_chars_in_current_chunk,
				original_chars_to_push + ori_chars_pushed,
				ori_chars_in_current_chunk ); 

			num_new_chars_to_push -= text_chars_in_current_chunk;
			num_ori_chars_to_push -= ori_chars_in_current_chunk;

			CHK_DATA( new_chk_idx ).len += num_chars_pushing;
		}


		if( adding_new_chunk )
		{
			link_chunks( te, current_index, new_chk_idx );
			current_index = new_chk_idx;
		}
	}

	if( first_chunk_next_index != -1 )
		link_chunks( te, current_index, first_chunk_next_index );
}


void get_chunk_index_from_pos( te_state * te, int pos, int * chunk_index, 
	int * in_chunk_index )
{
	int looking_at_index = get_first_active_chunk( te );
	INFINITE_LOOP
	{
		if( pos > CHK_DATA( looking_at_index ).len )
		{
			pos -= CHK_DATA( looking_at_index ).len;
			looking_at_index = CHK_DATA( looking_at_index ).index_next;
			if( looking_at_index == -1 ) 
			{
				(*chunk_index) = -1;
				(*in_chunk_index) = -1;
				return;
			}
		}
		else
		{
			(*chunk_index) = looking_at_index;
			(*in_chunk_index) = pos;
			return;
		}
	}
}

int get_pos_from_chunk_index( te_state * te, int chunk_index, 
	int in_chunk_index )
{
	int looking_at_index = get_first_active_chunk( te );
	int pos = in_chunk_index;

	INFINITE_LOOP
	{
		if( looking_at_index != chunk_index )
		{
			pos += CHK_DATA( looking_at_index ).len;
			looking_at_index = CHK_DATA( looking_at_index ).index_next;
		}
		else
		{
			return pos;
		}
	}
}

void insert_at_pos( te_state * te, char * text, int pos )
{
	int chunk_index = -1;
	int in_chunk_index = -1;
	get_chunk_index_from_pos( te, pos, &chunk_index, &in_chunk_index );

	if( chunk_index == -1 )
	{
		chunk_index = get_first_inactive_chunk( te );
		in_chunk_index = 0;
	}

	insert_at_chunk( te, text, strlen(text), chunk_index, in_chunk_index );
}



void remove_at_chunk( te_state * te, int rm_len, int chunk_index, int
	in_chunk_index )
{
	int chars_to_remove = rm_len;

	int current_index = chunk_index;
	int current_chars_to_remove = 0;

	int pos_remove_after = in_chunk_index;
	
	if( chars_to_remove > 0 )
	{
		INFINITE_LOOP
		{
			if( current_index == -1 ) break;

			current_chars_to_remove = MIN( CHK_DATA( current_index ).len -
				pos_remove_after, chars_to_remove );
			chars_to_remove -= current_chars_to_remove;

			// remove entire chunk
			if( current_chars_to_remove == CHK_DATA( current_index ).len )
			{
				link_chunks( te, CHK_DATA( current_index ).index_previous,
					CHK_DATA( current_index ).index_next );

				int old_index = current_index;
				current_index = CHK_DATA( current_index ).index_next;

				CHK_DATA( old_index ).len = 0;
				CHK_DATA( old_index ).index_next = -1;
				CHK_DATA( old_index ).index_previous = -1;
				te->num_active_chunks--;
#ifdef DEBUG
				memset( CHK_MEM( old_index ), 0, TEXT_CHUNK_SIZE );
#endif

			}
			else
			{
				CHK_DATA( current_index ).len -= current_chars_to_remove;

				int move_mem_back_dest = pos_remove_after;
				int move_mem_back_source = pos_remove_after +
					current_chars_to_remove;

				memmove( CHK_MEM( current_index ) + move_mem_back_dest, CHK_MEM(
						current_index ) + move_mem_back_source,
					TEXT_CHUNK_SIZE - move_mem_back_source );

#ifdef DEBUG
				memset( CHK_MEM( current_index ) + CHK_DATA( current_index).len,
					0, TEXT_CHUNK_SIZE - CHK_DATA( current_index).len );
#endif

				current_index = CHK_DATA( current_index ).index_next;
			}


			pos_remove_after = 0;
		}
	}
}




// NOTE: end_of_text referes to the character after the last character
inline void index_next( te_state * te, int * out_chunk_index, 
	int * out_in_chunk_index, bool * end_of_text = nullptr, int amount = 1 )
{
	if( end_of_text ) (*end_of_text) = false;

	while( amount > 0 )
	{
		int move_this_chunk = MIN( amount, CHK_DATA( (*out_chunk_index) ).len - 1 -
			(*out_in_chunk_index ) ); 
		(*out_in_chunk_index) += move_this_chunk;
		amount -= move_this_chunk;

		if( CHK_DATA( *out_chunk_index ).index_next == -1 &&
			(*out_in_chunk_index) == CHK_DATA( *out_chunk_index).len - 1 &&
			amount > 0 )
		{
			if( end_of_text ) (*end_of_text) = true;
			(*out_in_chunk_index) = CHK_DATA( (*out_chunk_index) ).len;

		}

		if( amount == 0 || CHK_DATA( *out_chunk_index ).index_next == -1 ) return;

		(*out_in_chunk_index) = -1;
		(*out_chunk_index) = CHK_DATA( (*out_chunk_index) ).index_next;
	}
}
inline void index_previous( te_state * te, int * out_chunk_index, 
	int * out_in_chunk_index, bool * beginning_of_text = nullptr, int amount = 1 )
{
	if( beginning_of_text ) (*beginning_of_text) = false;

	while( amount > 0 )
	{
		int move_this_chunk = MIN( amount, (*out_in_chunk_index ) ); 
		(*out_in_chunk_index) -= move_this_chunk;
		amount -= move_this_chunk;

		if( CHK_DATA( *out_chunk_index ).index_previous == -1 &&
			(*out_in_chunk_index) == 0 )
		{
			if( beginning_of_text ) (*beginning_of_text) = true;
		}

		if( amount == 0 || CHK_DATA( *out_chunk_index ).index_previous == -1 ) return;

		(*out_chunk_index) = CHK_DATA( (*out_chunk_index) ).index_previous;
		(*out_in_chunk_index) = CHK_DATA( (*out_chunk_index) ).len;
	}
}





void search_forward_at_chunk( te_state * te, char * text, int text_len, 
	int starting_chunk_index, int starting_in_chunk_index, int * out_chunk_index, int * out_in_chunk_index )
{
	int looking_at_index = starting_chunk_index;
	int looking_at_in_index = starting_in_chunk_index;

	while( looking_at_index != -1 )
	{
		int on_char_looking_at = 0;
		int current_looking_index = looking_at_index;
		int current_looking_in_index = looking_at_in_index;

		while( text[ on_char_looking_at ] == *(CHK_MEM( current_looking_index ) + current_looking_in_index ) )
		{
			if( on_char_looking_at == text_len - 1 )
			{
				(*out_chunk_index) = looking_at_index;
				(*out_in_chunk_index) = looking_at_in_index;
				return;
			}
			on_char_looking_at++;
			index_next( te, &current_looking_index, &current_looking_in_index );
			if( current_looking_index == -1 )
				return;
		}

		index_next( te, &looking_at_index, &looking_at_in_index );
	}
}





void search_backward_at_chunk( te_state * te, char * text, int text_len, 
	int starting_chunk_index, int starting_in_chunk_index, int * out_chunk_index, int * out_in_chunk_index )
{
	int looking_at_index = starting_chunk_index;
	int looking_at_in_index = starting_in_chunk_index;

	while( looking_at_index != -1 )
	{
		int on_char_looking_at = text_len - 1;
		int current_looking_index = looking_at_index;
		int current_looking_in_index = looking_at_in_index;
		bool beginning = false;

		while( text[ on_char_looking_at ] == *(CHK_MEM( current_looking_index ) + current_looking_in_index ) )
		{
			if( on_char_looking_at == 0 )
			{
				(*out_chunk_index) = current_looking_index;
				(*out_in_chunk_index) = current_looking_in_index;
				return;
			}
			on_char_looking_at--;
			if( beginning ) break;
			index_previous( te, &current_looking_index, &current_looking_in_index, &beginning );
		}

		index_previous( te, &looking_at_index, &looking_at_in_index );
	}
}





void sort_chunks( te_state * te )
{
	LOOP( te->num_active_chunks, i )
	{
		int index_to_realign = -1;
		if( i == 0 )
			index_to_realign = get_index_of_first_text_chunk( te );
		else
			index_to_realign = CHK_DATA( i - 1 ).index_next;


		if( index_to_realign != i )
		{
			// swap the text chunk memory, and the text chunk data structs
			{
				char swap_text_chars[ TEXT_CHUNK_SIZE ];
				text_chunk_data swap_tcd;

				memmove( swap_text_chars, CHK_MEM( i ), TEXT_CHUNK_SIZE );
				swap_tcd = CHK_DATA( i );

				memmove( CHK_MEM( i ), CHK_MEM( index_to_realign ), TEXT_CHUNK_SIZE );
				CHK_DATA( i ) = CHK_DATA( index_to_realign );

				memmove( CHK_MEM( index_to_realign ), swap_text_chars,
					TEXT_CHUNK_SIZE ); CHK_DATA( index_to_realign ) = swap_tcd;
			}

			

			// modify the next and previous indicies
			if( CHK_DATA( i ).index_previous != -1 )
			{
				if( CHK_DATA( i ).index_previous != i )
					CHK_DATA( CHK_DATA( i ).index_previous ).index_next = i;
				else
				{
					CHK_DATA( i ).index_previous = index_to_realign;
					CHK_DATA( index_to_realign ).index_next = i;
				}
			}
			if( CHK_DATA( i ).index_next != -1 )
			{
				if( CHK_DATA( i ).index_next != i )
					CHK_DATA( CHK_DATA( i ).index_next ).index_previous = i;
				else
				{
					CHK_DATA( i ).index_next = index_to_realign;
					CHK_DATA( index_to_realign ).index_previous = i;
				}
			}

			if( CHK_DATA( index_to_realign ).index_previous != -1 )
			{
				if( CHK_DATA( index_to_realign ).index_previous != index_to_realign )
					CHK_DATA( CHK_DATA( index_to_realign ).index_previous
						).index_next = index_to_realign;
			}
			if( CHK_DATA( index_to_realign ).index_next != -1 )
			{
				if( CHK_DATA( index_to_realign ).index_next != index_to_realign )
					CHK_DATA( CHK_DATA( index_to_realign ).index_next
						).index_previous = index_to_realign;
			}
		}
	}
}

// NOTE: assuming the text chunks are already sorted
void compress_mem( te_state * te )
{
	int looking_at_index = 0;
	int last_index = te->num_active_chunks - 1;

	while( looking_at_index <= last_index )
	{
		int num_chars_to_pull_in_from_next_chunks = TEXT_CHUNK_SIZE - 
			CHK_DATA( looking_at_index ).len;
		int num_chars_retrieved = 0;
		int next_chunk_index = looking_at_index + 1;
		while( num_chars_retrieved < num_chars_to_pull_in_from_next_chunks )
		{
			if( next_chunk_index > last_index ) break;

			int chars_to_pull_from_this_chunk = MIN( CHK_DATA( next_chunk_index ).len,
				num_chars_to_pull_in_from_next_chunks );
			memmove( CHK_MEM( looking_at_index ) + CHK_DATA( looking_at_index ).len, CHK_MEM(
				next_chunk_index ), chars_to_pull_from_this_chunk );
			CHK_DATA( looking_at_index ).len += chars_to_pull_from_this_chunk;
			CHK_DATA( next_chunk_index ).len -= chars_to_pull_from_this_chunk;

			if( CHK_DATA( next_chunk_index ).len != 0 )
			{
				memmove( CHK_MEM( next_chunk_index ), CHK_MEM( next_chunk_index
						) + chars_to_pull_from_this_chunk,
					TEXT_CHUNK_SIZE - chars_to_pull_from_this_chunk );
			}

			num_chars_to_pull_in_from_next_chunks -= chars_to_pull_from_this_chunk;

			next_chunk_index++;
		}
		looking_at_index++;
	}


	// delete all the dangling chunks
	LOOP_BACKWARDS( te->num_active_chunks, i )
	{
		if( CHK_DATA( i ).len == 0 )
		{
			te->num_active_chunks--;
			CHK_DATA( i ).index_next = -1;
			CHK_DATA( i ).index_previous = -1;
#ifdef DEBUG
			memset( CHK_MEM( i ), 0, TEXT_CHUNK_SIZE );
#endif
		}
		else
		{
#ifdef DEBUG
			memset( CHK_MEM( i ) + CHK_DATA( i ).len, 0, TEXT_CHUNK_SIZE -
				CHK_DATA( i ).len );
#endif
			break;
		}
	}
}

} // namespace _internal }}} 

// Public Functions {{{
// --------------------


// start editing a new file with name
void enew( te_state * te, char * file_name = nullptr );
// edit existing file
void edit( te_state * te, char * file_name );

void save( te_state * te );
void quit( te_state * te );
void save_and_quit( te_state * te );

void insert( te_state * te, char * text );
// IMPLEMENT: void insert_on_new_line_above( te_state * te, char * text );
// IMPLEMENT: void insert_on_new_line_below( te_state * te, char * text );


enum class text_move 
{
	beginning,
	end,
	beginning_of_line,
	end_of_line,
	previous_line,
	next_line,
};

// move cursor around screen
void move( te_state * te, text_move dst );

enum class search_direction
{
	forward,
	backward,
};

enum class cursor_land_after_search
{
	beginning,
	end,
};

// IMPLEMENT: void search( te_state * te, char * text, 
//		search_direction direction = search_direction::forward,
//		cursor_land_after_search land = cursor_land_after_search::beginning );

void search_forward( te_state * te, char * text, 
	cursor_land_after_search land = cursor_land_after_search::beginning );
void search_backward( te_state * te, char * text, 
	cursor_land_after_search land = cursor_land_after_search::beginning );


void delete_line( te_state * te );


// }}}

// Implementations {{{
// -------------------


void enew( te_state * te, char * file_name )
{
	using namespace _internal;

	mem_resize( te, 16 );
	strcpy( te->file_name, file_name );
}

void edit( te_state * te, char * file_name )
{
	using namespace _internal;

	{
		FILE * read_file = fopen( file_name, "rb" );

		if( !read_file )
			return;

		int len_text = get_file_size( read_file );

		// NOTE: I'm removing the last new line character
		len_text--;

		int len_text_mem = len_text;

		if( len_text % TEXT_CHUNK_SIZE != 0 )
			len_text_mem += TEXT_CHUNK_SIZE - ( len_text % TEXT_CHUNK_SIZE );

		int num_chunks = len_text_mem / TEXT_CHUNK_SIZE;
		int len_tc_data_mem = num_chunks * sizeof( text_chunk_data );
		int len_mem = len_text_mem + len_tc_data_mem;

		if( te->text_chunks )
			free( te->text_chunks );

		te->text_mem = (char*)malloc( len_mem );
		te->tc_data = (text_chunk_data*)((char*)te->text_mem + len_text_mem );

		te->num_text_chunks = num_chunks;
		te->num_active_chunks = num_chunks;

		LOOP( te->num_active_chunks - 1, i )
		{
			CHK_DATA( i ).len = TEXT_CHUNK_SIZE;
			CHK_DATA( i ).index_previous = i - 1;
			CHK_DATA( i ).index_next = i + 1;
		}
		int last_chunk_len = len_text % TEXT_CHUNK_SIZE;
		if( last_chunk_len == 0 ) last_chunk_len = 4;

		CHK_DATA( te->num_active_chunks - 1 ).len = last_chunk_len;
		CHK_DATA( te->num_active_chunks - 1 ).index_previous = 
			MAX( te->num_active_chunks - 2, -1 );
		CHK_DATA( te->num_active_chunks - 1 ).index_next = -1;

		fread( te->text_chunks, sizeof(char), len_text, read_file );

		fclose( read_file );

#ifdef DEBUG
		memset( te->text_chunks + len_text, 0, len_text_mem - len_text );
#endif
	}

	strcpy( te->file_name, file_name );
}

void save( te_state * te )
{
	using namespace _internal;

	sort_chunks( te );
	compress_mem( te );

	int len = te->num_active_chunks * TEXT_CHUNK_SIZE - ( TEXT_CHUNK_SIZE -
		CHK_DATA( te->num_active_chunks - 1 ).len );

	FILE * file = fopen( te->file_name, "wb" );
	fwrite( te->text_chunks, sizeof( char ), len, file );
	fwrite( "\n", sizeof( char ), 1, file );
	fclose( file );
}

void quit( te_state * te )
{
	free( te->text_mem );
	memset( te, 0, sizeof( te_state ) );

	te->num_text_chunks = 0;
	te->num_active_chunks = 0;

	te->cursor_position = 0;
}

void save_and_quit( te_state * te )
{
	save( te );
	quit( te );
}




void insert( te_state * te, char * text )
{
	using namespace _internal;

	insert_at_pos( te, text, te->cursor_position );
	te->cursor_position += strlen( text );
}



void move( te_state * te, text_move dst )
{
	using namespace _internal;

	if( dst == text_move::beginning )
	{
		te->cursor_position = 0;
	}
	else if( dst == text_move::end )
	{
		te->cursor_position = 0;

		int looking_at_index = get_first_active_chunk( te );
		LOOP( te->num_active_chunks, i )
		{
			te->cursor_position += CHK_DATA( looking_at_index ).len;
			looking_at_index = CHK_DATA( looking_at_index ).index_next;
		}
	}
	else if( dst == text_move::previous_line  || dst == text_move::next_line )
	{
		int chunk_index = -1;
		int in_chunk_index = -1;
		get_chunk_index_from_pos( te, te->cursor_position, &chunk_index,
			&in_chunk_index );

		int line_pos = 0;
		{
			if( !( in_chunk_index == 0 && CHK_DATA( chunk_index ).index_previous
					== -1 ) )
			{
				int ci = chunk_index;
				int ici = in_chunk_index; 

				INFINITE_LOOP
				{
					bool beginning;
					index_previous( te, &ci, &ici, &beginning );

					if( (*(CHK_MEM( ci ) + ici)) == '\n' ) break;

					line_pos++;

					if( beginning ) break;
				}
			}
		}


		if( dst == text_move::previous_line )
		{
			{
				int ci = chunk_index;
				int ici = in_chunk_index;

				while( (*(CHK_MEM( ci ) + ici )) != '\n' )
				{
					bool beginning;
					index_previous( te, &ci, &ici, &beginning );
					if( beginning ) return;
				}

				chunk_index = ci;
				in_chunk_index = ici;
			}

			bool is_previous_char_new_line = false;
			INFINITE_LOOP
			{
				int ci = chunk_index;
				int ici = in_chunk_index;
				bool beginning;
				index_previous( te, &ci, &ici, &beginning );

				is_previous_char_new_line = 
					(*(CHK_MEM( ci ) + ici )) == '\n';

				if( is_previous_char_new_line )
					break;

				chunk_index = ci;
				in_chunk_index = ici;

				if( beginning ) break;
			}
		}
		else
		{
			int ci = chunk_index;
			int ici = in_chunk_index;
			
			while( (*(CHK_MEM( ci ) + ici )) != '\n' )
			{
				bool end;
				index_next( te, &ci, &ici, &end );
				if( end ) return;
			}

			chunk_index = ci;
			in_chunk_index = ici;
			index_next( te, &chunk_index, &in_chunk_index );
		}


		LOOP( line_pos, i )
		{
			bool end;
			index_next( te, &chunk_index, &in_chunk_index, &end );
			if( end ) break;
			if( (*(CHK_MEM( chunk_index ) + in_chunk_index )) == '\n' ) break;
		}

		te->cursor_position = get_pos_from_chunk_index( te, chunk_index,
			in_chunk_index );
	}
	else if( dst == text_move::next_line )
	{
		int chunk_index = -1;
		int in_chunk_index = -1;
		get_chunk_index_from_pos( te, te->cursor_position, &chunk_index,
			&in_chunk_index );

		while( (*(CHK_MEM( chunk_index ) + in_chunk_index)) != '\n' )
		{
			bool end;
			index_next( te, &chunk_index, &in_chunk_index, &end );
			if( end ) break;
		}

		index_next( te, &chunk_index, &in_chunk_index  );

		te->cursor_position = get_pos_from_chunk_index( te, chunk_index,
			in_chunk_index );
	}
	else if( dst == text_move::beginning_of_line )
	{
		int chunk_index = -1;
		int in_chunk_index = -1;
		get_chunk_index_from_pos( te, te->cursor_position, &chunk_index,
			&in_chunk_index );

		INFINITE_LOOP
		{
			int prev_ci = chunk_index;
			int prev_ici = in_chunk_index;
			bool beginning;
			index_previous( te, &prev_ci, &prev_ici, &beginning );

			if( (*(CHK_MEM( prev_ci ) + prev_ici )) == '\n' ) break;

			chunk_index = prev_ci;
			in_chunk_index = prev_ici;

			if( beginning ) break;
		}

		te->cursor_position = get_pos_from_chunk_index( te, chunk_index,
			in_chunk_index );
	}
	else if( dst == text_move::end_of_line )
	{
		int chunk_index = -1;
		int in_chunk_index = -1;
		get_chunk_index_from_pos( te, te->cursor_position, &chunk_index,
			&in_chunk_index );

		while( (*(CHK_MEM( chunk_index ) + in_chunk_index)) != '\n' )
		{
			bool end;
			index_next( te, &chunk_index, &in_chunk_index, &end );
			if( end ) break;
		}

		te->cursor_position = get_pos_from_chunk_index( te, chunk_index,
			in_chunk_index );
	}
}


void search_forward( te_state * te, char * text, cursor_land_after_search land )
{
	using namespace _internal;

	int chunk_index = -1;
	int in_chunk_index = -1;

	get_chunk_index_from_pos( te, te->cursor_position, &chunk_index,
		&in_chunk_index );

	search_forward_at_chunk( te, text, strlen( text ), chunk_index, in_chunk_index,
		&chunk_index, &in_chunk_index );

	te->cursor_position = get_pos_from_chunk_index( te, chunk_index, in_chunk_index );

	if( land == cursor_land_after_search::end )
		te->cursor_position += strlen( text );
}


void search_backward( te_state * te, char * text, cursor_land_after_search land )
{
	using namespace _internal;

	int chunk_index = -1;
	int in_chunk_index = -1;

	get_chunk_index_from_pos( te, te->cursor_position, &chunk_index,
		&in_chunk_index );

	search_backward_at_chunk( te, text, strlen( text ), chunk_index, in_chunk_index,
		&chunk_index, &in_chunk_index );

	te->cursor_position = get_pos_from_chunk_index( te, chunk_index, in_chunk_index );

	if( land == cursor_land_after_search::end )
		te->cursor_position += strlen( text );
}

void delete_line( te_state * te )
{
	using namespace _internal;

	int chunk_index = -1;
	int in_chunk_index = -1;
	get_chunk_index_from_pos( te, te->cursor_position, &chunk_index,
		&in_chunk_index );

	if( (*(CHK_MEM( chunk_index ) + in_chunk_index)) == '\n' )
		index_previous( te, &chunk_index, &in_chunk_index );

	while( (*(CHK_MEM( chunk_index ) + in_chunk_index)) != '\n' &&
		( chunk_index > 0 || in_chunk_index > 0 ) )
	{
		index_previous( te, &chunk_index, &in_chunk_index );
	}

	int line_length = 0;
	{
		int c_idx = chunk_index;
		int ic_idx = in_chunk_index;

		index_next( te, &c_idx, &ic_idx );
		while( (*(CHK_MEM( c_idx ) + ic_idx )) != '\n' )
		{
			line_length++;
			index_next( te, &c_idx, &ic_idx );
		}
		line_length++;
	}

	index_next( te, &chunk_index, &in_chunk_index );

	remove_at_chunk( te, line_length, chunk_index, in_chunk_index );
}


#undef CHK_DATA
#undef CHK_MEM

// }}}

} // namespace text_editing
