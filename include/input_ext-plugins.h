/*****************************************************************************
 * input_ext-plugins.h: structures of the input not exported to other modules,
 *                      but exported to plug-ins
 *****************************************************************************
 * Copyright (C) 1999-2001 VideoLAN
 * $Id: input_ext-plugins.h,v 1.12 2001/12/27 01:49:34 massiot Exp $
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *****************************************************************************/

/*
 * Communication plugin -> input
 */

/* FIXME: you've gotta move this move this, you've gotta move this move this */
#define INPUT_READ_ONCE     7   /* We live in a world dominated by Ethernet. *
                                 * Ethernet MTU is 1500 bytes, so in a UDP   *
                                 * packet we can put : 1500/188 = 7 TS       *
                                 * packets. Have a nice day and merry Xmas.  */
#define PADDING_PACKET_SIZE 188 /* Size of the NULL packet inserted in case
                                 * of data loss (this should be < 188).      */
#define PADDING_PACKET_NUMBER 10 /* Number of padding packets top insert to
                                  * escape a decoder.                        */
#define NO_SEEK             -1

/*****************************************************************************
 * Prototypes from input_ext-dec.c
 *****************************************************************************/
void InitBitstream  ( struct bit_stream_s *, struct decoder_fifo_s *,
                      void (* pf_bitstream_callback)( struct bit_stream_s *,
                                                      boolean_t ),
                      void * p_callback_arg );
void NextDataPacket ( struct bit_stream_s * );

/*****************************************************************************
 * Prototypes from input_programs.c
 *****************************************************************************/
int  input_InitStream( struct input_thread_s *, size_t );
void input_EndStream ( struct input_thread_s * );
struct pgrm_descriptor_s * input_FindProgram( struct input_thread_s *, u16 );
struct pgrm_descriptor_s * input_AddProgram ( struct input_thread_s *,
                                              u16, size_t );
void input_DelProgram( struct input_thread_s *, struct pgrm_descriptor_s * );
int input_SetProgram( struct input_thread_s *, struct pgrm_descriptor_s * );
struct input_area_s * input_AddArea( struct input_thread_s * );
void input_DelArea   ( struct input_thread_s *, struct input_area_s * );
struct es_descriptor_s * input_FindES( struct input_thread_s *, u16 );
struct es_descriptor_s * input_AddES ( struct input_thread_s *,
                                       struct pgrm_descriptor_s *, u16,
                                       size_t );
void input_DelES     ( struct input_thread_s *, struct es_descriptor_s * );
int  input_SelectES  ( struct input_thread_s *, struct es_descriptor_s * );
int  input_UnselectES( struct input_thread_s *, struct es_descriptor_s * );

/*****************************************************************************
 * Prototypes from input_dec.c
 *****************************************************************************/
//decoder_capabilities_s * input_ProbeDecoder( void );
vlc_thread_t input_RunDecoder( struct input_thread_s *,
                               struct es_descriptor_s * );
void input_EndDecoder( struct input_thread_s *, struct es_descriptor_s * );
void input_DecodePES ( struct decoder_fifo_s *, struct pes_packet_s * );
void input_EscapeDiscontinuity( struct input_thread_s *,
                                struct pgrm_descriptor_s * );
void input_EscapeAudioDiscontinuity( struct input_thread_s * );

/*****************************************************************************
 * Prototypes from input_clock.c
 *****************************************************************************/
void input_ClockInit( struct pgrm_descriptor_s * );
int  input_ClockManageControl( struct input_thread_s *,
                               struct pgrm_descriptor_s *, mtime_t );
void input_ClockManageRef( struct input_thread_s *,
                           struct pgrm_descriptor_s *, mtime_t );
mtime_t input_ClockGetTS( struct input_thread_s *,
                          struct pgrm_descriptor_s *, mtime_t );

/*****************************************************************************
 * Create a NULL packet for padding in case of a data loss
 *****************************************************************************/
static __inline__ void input_NullPacket( input_thread_t * p_input,
                                         es_descriptor_t * p_es )
{
    data_packet_t *             p_pad_data;
    pes_packet_t *              p_pes;

    if( (p_pad_data = p_input->pf_new_packet(
                    p_input->p_method_data,
                    PADDING_PACKET_SIZE )) == NULL )
    {
        intf_ErrMsg("Out of memory");
        p_input->b_error = 1;
        return;
    }

    memset( p_pad_data->p_payload_start, 0, PADDING_PACKET_SIZE );
    p_pad_data->b_discard_payload = 1;
    p_pes = p_es->p_pes;

    if( p_pes != NULL )
    {
        p_pes->b_discontinuity = 1;
        p_pes->p_last->p_next = p_pad_data;
        p_pes->p_last = p_pad_data;
        p_pes->i_nb_data++;
    }
    else
    {
        if( (p_pes = p_input->pf_new_pes( p_input->p_method_data )) == NULL )
        {
            intf_ErrMsg("Out of memory");
            p_input->b_error = 1;
            return;
        }

        p_pes->i_rate = p_input->stream.control.i_rate;
        p_pes->p_first = p_pes->p_last = p_pad_data;
        p_pes->i_nb_data = 1;
        p_pes->b_discontinuity = 1;
        input_DecodePES( p_es->p_decoder_fifo, p_pes );
    }
}


/*
 * Optional Next Generation buffer manager
 *
 * Either buffers can only be used in one data packet (PS case), or buffers
 * contain several data packets (DVD case). In the first case, buffers are
 * embedded into data packets, otherwise they are allocated separately and
 * shared with a refcount. --Meuuh
 */

/* Number of buffers for the calculation of the mean */
#define INPUT_BRESENHAM_NB      50

/* Flags */
#define BUFFERS_NOFLAGS         0
#define BUFFERS_UNIQUE_SIZE     1 /* Only with NB_LIFO == 1 */

/*****************************************************************************
 * input_buffers_t: defines a LIFO per data type to keep
 *****************************************************************************/
#define PACKETS_LIFO( TYPE, NAME )                                           \
struct                                                                      \
{                                                                           \
    TYPE * p_stack;                                                         \
    unsigned int i_depth;                                                   \
} NAME;

#define BUFFERS_LIFO( TYPE, NAME )                                          \
struct                                                                      \
{                                                                           \
    TYPE * p_stack; /* First item in the LIFO */                            \
    unsigned int i_depth; /* Number of items in the LIFO */                 \
    unsigned int i_average_size; /* Average size of the items (Bresenham) */\
} NAME;

#define DECLARE_BUFFERS_EMBEDDED( FLAGS, NB_LIFO )                          \
typedef struct input_buffers_s                                              \
{                                                                           \
    vlc_mutex_t lock;                                                       \
    PACKETS_LIFO( pes_packet_t, pes )                                       \
    BUFFERS_LIFO( data_packet_t, data[NB_LIFO] )                            \
    size_t i_allocated;                                                     \
} input_buffers_t;

#define DECLARE_BUFFERS_SHARED( FLAGS, NB_LIFO )                            \
typedef struct input_buffers_s                                              \
{                                                                           \
    vlc_mutex_t lock;                                                       \
    PACKETS_LIFO( pes_packet_t, pes )                                       \
    PACKETS_LIFO( data_packet_t, data )                                     \
    BUFFERS_LIFO( data_buffer_t, buffers[NB_LIFO] )                         \
    size_t i_allocated;                                                     \
} input_buffers_t;

typedef struct data_buffer_s
{
    int i_refcount;
    unsigned int i_size;
    struct data_buffer_s * p_next;
    byte_t payload_start;
} data_buffer_t;


/*****************************************************************************
 * input_BuffersInit: initialize the cache structures, return a pointer to it
 *****************************************************************************/
#define DECLARE_BUFFERS_INIT( FLAGS, NB_LIFO )                              \
static void * input_BuffersInit( void )                                     \
{                                                                           \
    input_buffers_t * p_buffers = malloc( sizeof( input_buffers_t ) );      \
                                                                            \
    if( p_buffers == NULL )                                                 \
    {                                                                       \
        return( NULL );                                                     \
    }                                                                       \
                                                                            \
    memset( p_buffers, 0, sizeof( input_buffers_t ) );                      \
    vlc_mutex_init( &p_buffers->lock );                                     \
                                                                            \
    return (void *)p_buffers;                                               \
}

/*****************************************************************************
 * input_BuffersEnd: free all cached structures
 *****************************************************************************/
#define BUFFERS_END_STAT_BUFFERS_LOOP( STRUCT )                             \
    for( i = 0; i < NB_LIFO; i++ )                                          \
    {                                                                       \
        if( FLAGS & BUFFERS_UNIQUE_SIZE )                                   \
        {                                                                   \
            intf_StatMsg(                                                   \
              "input buffers stats: " #STRUCT "[%d]: %d packets",           \
              i, p_buffers->STRUCT[i].i_depth );                            \
        }                                                                   \
        else                                                                \
        {                                                                   \
            intf_StatMsg(                                                   \
              "input buffers stats: " #STRUCT "[%d]: %d bytes, %d packets", \
              i, p_buffers->STRUCT[i].i_average_size,                       \
              p_buffers->STRUCT[i].i_depth );                               \
        }                                                                   \
    }

#define BUFFERS_END_STAT( FLAGS, NB_LIFO )                                  \
    BUFFERS_END_STAT_BUFFERS_LOOP( data );

#define BUFFERS_END_STAT_SHARED( FLAGS, NB_LIFO )                           \
    intf_StatMsg( "input buffers stats: data: %d packets",                  \
                  p_buffers->data.i_depth );                                \
    BUFFERS_END_STAT_BUFFERS_LOOP( buffers );


#define BUFFERS_END_BUFFERS_LOOP                                            \
    while( p_buf != NULL )                                                  \
    {                                                                       \
        p_next = p_buf->p_next;                                             \
        p_buffers->i_allocated -= p_buf->i_size;                            \
        free( p_buf );                                                      \
        p_buf = p_next;                                                     \
    }

#define BUFFERS_END_PACKETS_LOOP                                            \
    while( p_packet != NULL )                                               \
    {                                                                       \
        p_next = p_packet->p_next;                                          \
        free( p_packet );                                                   \
        p_packet = p_next;                                                  \
    }

#define BUFFERS_END_LOOP( FLAGS, NB_LIFO )                                  \
    for( i = 0; i < NB_LIFO; i++ )                                          \
    {                                                                       \
        data_packet_t * p_next;                                             \
        data_packet_t * p_buf = p_buffers->data[i].p_stack;                 \
        BUFFERS_END_BUFFERS_LOOP;                                           \
    }                                                                       \

#define BUFFERS_END_LOOP_SHARED( FLAGS, NB_LIFO )                           \
    {                                                                       \
        /* Free data packets */                                             \
        data_packet_t * p_next;                                             \
        data_packet_t * p_packet = p_buffers->data.p_stack;                 \
        BUFFERS_END_PACKETS_LOOP;                                           \
    }                                                                       \
                                                                            \
    for( i = 0; i < NB_LIFO; i++ )                                          \
    {                                                                       \
        data_buffer_t * p_next;                                             \
        data_buffer_t * p_buf = p_buffers->buffers[i].p_stack;              \
        BUFFERS_END_BUFFERS_LOOP;                                           \
    }                                                                       \

#define BUFFERS_END( FLAGS, NB_LIFO, STAT_LOOP, LOOP )                      \
static void input_BuffersEnd( void * _p_buffers )                           \
{                                                                           \
    input_buffers_t *   p_buffers = (input_buffers_t *)_p_buffers;          \
                                                                            \
    if( _p_buffers != NULL )                                                \
    {                                                                       \
        int i;                                                              \
                                                                            \
        if( p_main->b_stats )                                               \
        {                                                                   \
            int i;                                                          \
            intf_StatMsg( "input buffers stats: pes: %d packets",           \
                          p_buffers->pes.i_depth );                         \
            STAT_LOOP( FLAGS, NB_LIFO );                                    \
        }                                                                   \
                                                                            \
        {                                                                   \
            /* Free PES */                                                  \
            pes_packet_t * p_next, * p_packet = p_buffers->pes.p_stack;     \
            BUFFERS_END_PACKETS_LOOP;                                       \
        }                                                                   \
                                                                            \
        LOOP( FLAGS, NB_LIFO );                                             \
                                                                            \
        if( p_buffers->i_allocated )                                        \
        {                                                                   \
            intf_ErrMsg( "input buffers error: %d bytes have not been"      \
                         " freed, expect memory leak",                      \
                         p_buffers->i_allocated );                          \
        }                                                                   \
                                                                            \
        vlc_mutex_destroy( &p_buffers->lock );                              \
        free( _p_buffers );                                                 \
    }                                                                       \
}

#define DECLARE_BUFFERS_END( FLAGS, NB_LIFO )                               \
    BUFFERS_END( FLAGS, NB_LIFO, BUFFERS_END_STAT, BUFFERS_END_LOOP );

#define DECLARE_BUFFERS_END_SHARED( FLAGS, NB_LIFO )                        \
    BUFFERS_END( FLAGS, NB_LIFO, BUFFERS_END_STAT_SHARED,                   \
                 BUFFERS_END_LOOP_SHARED );

/*****************************************************************************
 * input_NewPacket: return a pointer to a data packet of the appropriate size
 *****************************************************************************/
#define BUFFERS_NEWPACKET_EXTRA_DECLARATION( FLAGS, NB_LIFO )               \
    data_packet_t **    pp_data = &p_buf;

#define BUFFERS_NEWPACKET_EXTRA_DECLARATION_SHARED( FLAGS, NB_LIFO )        \
    data_packet_t *     p_data;                                             \
    data_packet_t **    pp_data = &p_data;

#define BUFFERS_NEWPACKET_EXTRA( FLAGS, NB_LIFO )

#define BUFFERS_NEWPACKET_EXTRA_SHARED( FLAGS, NB_LIFO )                    \
    /* Find a data packet */                                                \
    if( p_buffers->data.p_stack != NULL )                                   \
    {                                                                       \
        p_data = p_buffers->data.p_stack;                                   \
        p_buffers->data.p_stack = p_data->p_next;                           \
        p_buffers->data.i_depth--;                                          \
    }                                                                       \
    else                                                                    \
    {                                                                       \
        p_data = malloc( sizeof( data_packet_t ) );                         \
        if( p_data == NULL )                                                \
        {                                                                   \
            intf_ErrMsg( "Out of memory" );                                 \
            vlc_mutex_unlock( &p_buffers->lock );                           \
            return( NULL );                                                 \
        }                                                                   \
    }                                                                       \
                                                                            \
    if( i_size == 0 )                                                       \
    {                                                                       \
        /* Warning : in that case, the data packet is left partly           \
         * uninitialized ; theorically only input_ShareBuffer may call      \
         * this. */                                                         \
        p_data->p_next = NULL;                                              \
        p_data->b_discard_payload = 0;                                      \
        return p_data;                                                      \
    }

#define BUFFERS_NEWPACKET_END( FLAGS, NB_LIFO )

#define BUFFERS_NEWPACKET_END_SHARED( FLAGS, NB_LIFO )                      \
    /* Initialize refcount */                                               \
    p_buf->i_refcount = 1;

#define BUFFERS_NEWPACKET( FLAGS, NB_LIFO, TYPE, NAME, EXTRA_DECLARATION,   \
                           EXTRA, END )                                     \
static __inline__ data_packet_t * _input_NewPacket( void * _p_buffers,      \
                                                    size_t i_size )         \
{                                                                           \
    input_buffers_t *   p_buffers = (input_buffers_t *)_p_buffers;          \
    int                 i_select;                                           \
    TYPE *              p_buf;                                              \
    EXTRA_DECLARATION( FLAGS, NB_LIFO );                                    \
                                                                            \
    /* Safety check */                                                      \
    if( p_buffers->i_allocated > INPUT_MAX_ALLOCATION )                     \
    {                                                                       \
        intf_ErrMsg( "INPUT_MAX_ALLOCATION reached (%d)",                   \
                     p_buffers->i_allocated );                              \
        return NULL;                                                        \
    }                                                                       \
                                                                            \
    EXTRA( FLAGS, NB_LIFO );                                                \
                                                                            \
    for( i_select = 0; i_select < NB_LIFO - 1; i_select++ )                 \
    {                                                                       \
        if( i_size <= (2 * p_buffers->NAME[i_select].i_average_size         \
                  + p_buffers->NAME[i_select + 1].i_average_size) / 3 )     \
        {                                                                   \
            break;                                                          \
        }                                                                   \
    }                                                                       \
                                                                            \
    if( p_buffers->NAME[i_select].p_stack != NULL )                         \
    {                                                                       \
        /* Take the packet from the cache */                                \
        p_buf = p_buffers->NAME[i_select].p_stack;                          \
        p_buffers->NAME[i_select].p_stack = p_buf->p_next;                  \
        p_buffers->NAME[i_select].i_depth--;                                \
                                                                            \
        /* Reallocate the packet if it is too small or too large */         \
        if( !(FLAGS & BUFFERS_UNIQUE_SIZE) &&                               \
            (p_buf->i_size < i_size || p_buf->i_size > 3 * i_size) )        \
        {                                                                   \
            p_buffers->i_allocated -= p_buf->i_size;                        \
            p_buf = realloc( p_buf, sizeof( TYPE ) + i_size );              \
            if( p_buf == NULL )                                             \
            {                                                               \
                intf_ErrMsg( "Out of memory" );                             \
                return NULL;                                                \
            }                                                               \
            p_buf->i_size = i_size;                                         \
            p_buffers->i_allocated += i_size;                               \
        }                                                                   \
    }                                                                       \
    else                                                                    \
    {                                                                       \
        /* Allocate a new packet */                                         \
        p_buf = malloc( sizeof( TYPE ) + i_size );                          \
        if( p_buf == NULL )                                                 \
        {                                                                   \
            intf_ErrMsg( "Out of memory" );                                 \
            return NULL;                                                    \
        }                                                                   \
        p_buf->i_size = i_size;                                             \
        p_buffers->i_allocated += i_size;                                   \
    }                                                                       \
                                                                            \
    /* Initialize data */                                                   \
    (*pp_data)->p_next = NULL;                                              \
    (*pp_data)->b_discard_payload = 0;                                      \
    (*pp_data)->p_buffer = (byte_t *)p_buf;                                 \
    (*pp_data)->p_demux_start = (*pp_data)->p_buffer + sizeof( TYPE );      \
    (*pp_data)->p_payload_start = (*pp_data)->p_demux_start;                \
    (*pp_data)->p_payload_end = (*pp_data)->p_payload_start + i_size;       \
                                                                            \
    END( FLAGS, NB_LIFO );                                                  \
                                                                            \
    return( *pp_data );                                                     \
}                                                                           \
                                                                            \
static data_packet_t * input_NewPacket( void * _p_buffers, size_t i_size )  \
{                                                                           \
    input_buffers_t *   p_buffers = (input_buffers_t *)_p_buffers;          \
    data_packet_t *     p_data;                                             \
                                                                            \
    /* Safety check */                                                      \
    if( !(FLAGS & BUFFERS_UNIQUE_SIZE) && i_size > INPUT_MAX_PACKET_SIZE )  \
    {                                                                       \
        intf_ErrMsg( "Packet too big (%d)", i_size );                       \
        return NULL;                                                        \
    }                                                                       \
                                                                            \
    vlc_mutex_lock( &p_buffers->lock );                                     \
    p_data = _input_NewPacket( _p_buffers, i_size );                        \
    vlc_mutex_unlock( &p_buffers->lock );                                   \
    return( p_data );                                                       \
}

#define DECLARE_BUFFERS_NEWPACKET( FLAGS, NB_LIFO )                         \
    BUFFERS_NEWPACKET( FLAGS, NB_LIFO, data_packet_t, data,                 \
            BUFFERS_NEWPACKET_EXTRA_DECLARATION, BUFFERS_NEWPACKET_EXTRA,   \
            BUFFERS_NEWPACKET_END )

#define DECLARE_BUFFERS_NEWPACKET_SHARED( FLAGS, NB_LIFO )                  \
    BUFFERS_NEWPACKET( FLAGS, NB_LIFO, data_buffer_t, buffers,              \
            BUFFERS_NEWPACKET_EXTRA_DECLARATION_SHARED,                     \
            BUFFERS_NEWPACKET_EXTRA_SHARED, BUFFERS_NEWPACKET_END_SHARED )

/*****************************************************************************
 * input_DeletePacket: put a packet back into the cache
 *****************************************************************************/
#define BUFFERS_DELETEPACKET_EXTRA( FLAGS, NB_LIFO, DATA_CACHE_SIZE )       \
    data_packet_t * p_buf = p_data;

#define BUFFERS_DELETEPACKET_EXTRA_SHARED( FLAGS, NB_LIFO, DATA_CACHE_SIZE )\
    data_buffer_t * p_buf = (data_buffer_t *)p_data->p_buffer;              \
                                                                            \
    /* Get rid of the data packet */                                        \
    if( p_buffers->data.i_depth < DATA_CACHE_SIZE )                         \
    {                                                                       \
        /* Cache not full : store the packet in it */                       \
        p_data->p_next = p_buffers->data.p_stack;                           \
        p_buffers->data.p_stack = p_data;                                   \
        p_buffers->data.i_depth++;                                          \
    }                                                                       \
    else                                                                    \
    {                                                                       \
        free( p_data );                                                     \
    }                                                                       \
                                                                            \
    /* Decrement refcount */                                                \
    p_buf->i_refcount--;                                                    \
    if( p_buf->i_refcount > 0 )                                             \
    {                                                                       \
        return;                                                             \
    }

#define BUFFERS_DELETEPACKET( FLAGS, NB_LIFO, DATA_CACHE_SIZE, TYPE,        \
                              NAME, EXTRA )                                 \
static __inline__ void _input_DeletePacket( void * _p_buffers,              \
                                            data_packet_t * p_data )        \
{                                                                           \
    input_buffers_t *   p_buffers = (input_buffers_t *)_p_buffers;          \
    int                 i_select;                                           \
                                                                            \
    while( p_data != NULL )                                                 \
    {                                                                       \
        data_packet_t * p_next = p_data->p_next;                            \
                                                                            \
        EXTRA( FLAGS, NB_LIFO, DATA_CACHE_SIZE );                           \
                                                                            \
        for( i_select = 0; i_select < NB_LIFO - 1; i_select++ )             \
        {                                                                   \
            if( p_buf->i_size <=                                            \
                   (2 * p_buffers->NAME[i_select].i_average_size            \
                      + p_buffers->NAME[i_select + 1].i_average_size) / 3 ) \
            {                                                               \
                break;                                                      \
            }                                                               \
        }                                                                   \
                                                                            \
        if( p_buffers->NAME[i_select].i_depth < DATA_CACHE_SIZE )           \
        {                                                                   \
            /* Cache not full : store the packet in it */                   \
            p_buf->p_next = p_buffers->NAME[i_select].p_stack;              \
            p_buffers->NAME[i_select].p_stack = p_buf;                      \
            p_buffers->NAME[i_select].i_depth++;                            \
                                                                            \
            if( !(FLAGS & BUFFERS_UNIQUE_SIZE) )                            \
            {                                                               \
                /* Update Bresenham mean (very approximative) */            \
                p_buffers->NAME[i_select].i_average_size = ( p_buf->i_size  \
                     + p_buffers->NAME[i_select].i_average_size             \
                       * (INPUT_BRESENHAM_NB - 1) )                         \
                     / INPUT_BRESENHAM_NB;                                  \
            }                                                               \
        }                                                                   \
        else                                                                \
        {                                                                   \
            p_buffers->i_allocated -= p_buf->i_size;                        \
            free( p_buf );                                                  \
        }                                                                   \
                                                                            \
        p_data = p_next;                                                    \
    }                                                                       \
}                                                                           \
                                                                            \
static void input_DeletePacket( void * _p_buffers, data_packet_t * p_data ) \
{                                                                           \
    input_buffers_t *   p_buffers = (input_buffers_t *)_p_buffers;          \
                                                                            \
    vlc_mutex_lock( &p_buffers->lock );                                     \
    _input_DeletePacket( _p_buffers, p_data );                              \
    vlc_mutex_unlock( &p_buffers->lock );                                   \
}

#define DECLARE_BUFFERS_DELETEPACKET( FLAGS, NB_LIFO, DATA_CACHE_SIZE )     \
    BUFFERS_DELETEPACKET( FLAGS, NB_LIFO, DATA_CACHE_SIZE, data_packet_t,   \
                          data, BUFFERS_DELETEPACKET_EXTRA )

#define DECLARE_BUFFERS_DELETEPACKET_SHARED( FLAGS, NB_LIFO,                \
                                             DATA_CACHE_SIZE )              \
    BUFFERS_DELETEPACKET( FLAGS, NB_LIFO, DATA_CACHE_SIZE, data_buffer_t,   \
                          buffers, BUFFERS_DELETEPACKET_EXTRA_SHARED )

/*****************************************************************************
 * input_NewPES: return a pointer to a new PES packet
 *****************************************************************************/
#define DECLARE_BUFFERS_NEWPES( FLAGS, NB_LIFO )                            \
static pes_packet_t * input_NewPES( void * _p_buffers )                     \
{                                                                           \
    input_buffers_t *   p_buffers = (input_buffers_t *)_p_buffers;          \
    pes_packet_t *      p_pes;                                              \
                                                                            \
    vlc_mutex_lock( &p_buffers->lock );                                     \
                                                                            \
    if( p_buffers->pes.p_stack != NULL )                                    \
    {                                                                       \
        p_pes = p_buffers->pes.p_stack;                                     \
        p_buffers->pes.p_stack = p_pes->p_next;                             \
        p_buffers->pes.i_depth--;                                           \
    }                                                                       \
    else                                                                    \
    {                                                                       \
        p_pes = malloc( sizeof( pes_packet_t ) );                           \
        if( p_pes == NULL )                                                 \
        {                                                                   \
            intf_ErrMsg( "Out of memory" );                                 \
            vlc_mutex_unlock( &p_buffers->lock );                           \
            return( NULL );                                                 \
        }                                                                   \
    }                                                                       \
                                                                            \
    vlc_mutex_unlock( &p_buffers->lock );                                   \
                                                                            \
    /* Initialize data */                                                   \
    p_pes->p_next = NULL;                                                   \
    p_pes->b_data_alignment = p_pes->b_discontinuity =                      \
        p_pes->i_pts = p_pes->i_dts = 0;                                    \
    p_pes->i_pes_size = 0;                                                  \
    p_pes->p_first = p_pes->p_last = NULL;                                  \
    p_pes->i_nb_data = 0;                                                   \
                                                                            \
    return( p_pes );                                                        \
}

/*****************************************************************************
 * input_DeletePES: put a pes and all data packets back into the cache
 *****************************************************************************/
#define BUFFERS_DELETEPES_EXTRA( FLAGS, NB_LIFO, DATA_CACHE_SIZE )          \
    /* Small hopeless optimization */                                       \
    if( (FLAGS & BUFFERS_UNIQUE_SIZE)                                       \
           && p_buffers->data[0].i_depth < DATA_CACHE_SIZE )                \
    {                                                                       \
        p_buffers->data[0].i_depth += p_pes->i_nb_data;                     \
        p_pes->p_last->p_next = p_buffers->data[0].p_stack;                 \
        p_buffers->data[0].p_stack = p_pes->p_first;                        \
    }                                                                       \
    else /* No semicolon after this or you will die */ 

#define BUFFERS_DELETEPES_EXTRA_SHARED( FLAGS, NB_LIFO, DATA_CACHE_SIZE )

#define BUFFERS_DELETEPES( FLAGS, NB_LIFO, DATA_CACHE_SIZE, PES_CACHE_SIZE, \
                           EXTRA )                                          \
static void input_DeletePES( void * _p_buffers, pes_packet_t * p_pes )      \
{                                                                           \
    input_buffers_t *   p_buffers = (input_buffers_t *)_p_buffers;          \
                                                                            \
    vlc_mutex_lock( &p_buffers->lock );                                     \
                                                                            \
    while( p_pes != NULL )                                                  \
    {                                                                       \
        pes_packet_t * p_next = p_pes->p_next;                              \
                                                                            \
        /* Delete all data packets - no semicolon, PLEASE */                \
        EXTRA( FLAGS, NB_LIFO, DATA_CACHE_SIZE )                            \
        {                                                                   \
            _input_DeletePacket( _p_buffers, p_pes->p_first );              \
        }                                                                   \
                                                                            \
        if( p_buffers->pes.i_depth < PES_CACHE_SIZE )                       \
        {                                                                   \
            /* Cache not full : store the packet in it */                   \
            p_pes->p_next = p_buffers->pes.p_stack;                         \
            p_buffers->pes.p_stack = p_pes;                                 \
            p_buffers->pes.i_depth++;                                       \
        }                                                                   \
        else                                                                \
        {                                                                   \
            free( p_pes );                                                  \
        }                                                                   \
                                                                            \
        p_pes = p_next;                                                     \
    }                                                                       \
                                                                            \
    vlc_mutex_unlock( &p_buffers->lock );                                   \
}

#define DECLARE_BUFFERS_DELETEPES( FLAGS, NB_LIFO, DATA_CACHE_SIZE,         \
                                   PES_CACHE_SIZE )                         \
    BUFFERS_DELETEPES( FLAGS, NB_LIFO, DATA_CACHE_SIZE, PES_CACHE_SIZE,     \
                       BUFFERS_DELETEPES_EXTRA )

#define DECLARE_BUFFERS_DELETEPES_SHARED( FLAGS, NB_LIFO, DATA_CACHE_SIZE,  \
                                          PES_CACHE_SIZE )                  \
    BUFFERS_DELETEPES( FLAGS, NB_LIFO, DATA_CACHE_SIZE, PES_CACHE_SIZE,     \
                       BUFFERS_DELETEPES_EXTRA_SHARED )

/*****************************************************************************
 * input_BuffersToIO: return an io vector (only with BUFFERS_UNIQUE_SIZE)
 *****************************************************************************/
#define DECLARE_BUFFERS_TOIO( FLAGS, BUFFER_SIZE )                          \
static data_packet_t * input_BuffersToIO( void * _p_buffers,                \
                                         struct iovec * p_iovec, int i_nb ) \
{                                                                           \
    input_buffers_t *   p_buffers = (input_buffers_t *)_p_buffers;          \
    data_packet_t *     p_data = NULL;                                      \
    int                 i;                                                  \
                                                                            \
    vlc_mutex_lock( &p_buffers->lock );                                     \
                                                                            \
    for( i = i_nb - 1; i >= 0; i-- )                                        \
    {                                                                       \
        data_packet_t * p_next = _input_NewPacket( _p_buffers,              \
                                          BUFFER_SIZE /* UNIQUE_SIZE */ );  \
        if( p_next == NULL )                                                \
        {                                                                   \
            while( p_data != NULL )                                         \
            {                                                               \
                p_next = p_data->p_next;                                    \
                _input_DeletePacket( _p_buffers, p_data );                  \
                p_data = p_next;                                            \
            }                                                               \
            return( NULL );                                                 \
        }                                                                   \
                                                                            \
        p_iovec[i].iov_base = p_next->p_demux_start;                        \
        p_iovec[i].iov_len = BUFFER_SIZE;                                   \
        p_next->p_next = p_data;                                            \
        p_data = p_next;                                                    \
    }                                                                       \
                                                                            \
    vlc_mutex_unlock( &p_buffers->lock );                                   \
                                                                            \
    return( p_data );                                                       \
}

/*****************************************************************************
 * input_ShareBuffer: return a new data_packet to the same buffer
 *****************************************************************************/
#define DECLARE_BUFFERS_SHAREBUFFER( FLAGS )                                \
static data_packet_t * input_ShareBuffer( void * _p_buffers,                \
                                           data_packet_t * p_shared_data )  \
{                                                                           \
    input_buffers_t *   p_buffers = (input_buffers_t *)_p_buffers;          \
    data_packet_t *     p_data;                                             \
    data_buffer_t *     p_buf = (data_buffer_t *)p_shared_data->p_buffer;   \
                                                                            \
    vlc_mutex_lock( &p_buffers->lock );                                     \
                                                                            \
    /* Get new data_packet_t */                                             \
    p_data = _input_NewPacket( _p_buffers, 0 );                             \
                                                                            \
    /* Finish initialization of p_data */                                   \
    p_data->p_buffer = p_shared_data->p_buffer;                             \
    p_data->p_demux_start = p_data->p_payload_start                         \
                = p_shared_data->p_buffer + sizeof( data_buffer_t );        \
    p_data->p_payload_end = p_shared_data->p_buffer + p_buf->i_size;        \
                                                                            \
    /* Update refcount */                                                   \
    p_buf->i_refcount++;                                                    \
                                                                            \
    vlc_mutex_unlock( &p_buffers->lock );                                   \
                                                                            \
    return( p_data );                                                       \
}


/*
 * Optional MPEG demultiplexing
 */

/*****************************************************************************
 * Constants
 *****************************************************************************/
#define TS_PACKET_SIZE      188                       /* Size of a TS packet */
#define PSI_SECTION_SIZE    4096            /* Maximum size of a PSI section */

#define PAT_UNINITIALIZED    (1 << 6)
#define PMT_UNINITIALIZED    (1 << 6)

#define PSI_IS_PAT          0x00
#define PSI_IS_PMT          0x01
#define UNKNOWN_PSI         0xff

/*****************************************************************************
 * psi_section_t
 *****************************************************************************
 * Describes a PSI section. Beware, it doesn't contain pointers to the TS
 * packets that contain it as for a PES, but the data themselves
 *****************************************************************************/
typedef struct psi_section_s
{
    byte_t                  buffer[PSI_SECTION_SIZE];

    u8                      i_section_number;
    u8                      i_last_section_number;
    u8                      i_version_number;
    u16                     i_section_length;
    u16                     i_read_in_section;
    
    /* the PSI is complete */
    boolean_t               b_is_complete;
    
    /* packet missed up ? */
    boolean_t               b_trash;

    /*about sections  */ 
    boolean_t               b_section_complete;

    /* where are we currently ? */
    byte_t                * p_current;

} psi_section_t;

/*****************************************************************************
 * es_ts_data_t: extension of es_descriptor_t
 *****************************************************************************/
typedef struct es_ts_data_s
{
    boolean_t               b_psi;   /* Does the stream have to be handled by
                                      *                    the PSI decoder ? */

    int                     i_psi_type;  /* There are different types of PSI */
    
    psi_section_t *         p_psi_section;                    /* PSI packets */

    /* Markers */
    int                     i_continuity_counter;
} es_ts_data_t;

/*****************************************************************************
 * pgrm_ts_data_t: extension of pgrm_descriptor_t
 *****************************************************************************/
typedef struct pgrm_ts_data_s
{
    u16                     i_pcr_pid;             /* PCR ES, for TS streams */
    int                     i_pmt_version;
} pgrm_ts_data_t;

/*****************************************************************************
 * stream_ts_data_t: extension of stream_descriptor_t
 *****************************************************************************/
typedef struct stream_ts_data_s
{
    int i_pat_version;          /* Current version of the PAT */
} stream_ts_data_t;

/*****************************************************************************
 * stream_ps_data_t: extension of stream_descriptor_t
 *****************************************************************************/
typedef struct stream_ps_data_s
{
    boolean_t               b_has_PSM;                 /* very rare, in fact */

    u8                      i_PSM_version;
} stream_ps_data_t;

/* PSM version is 5 bits, so -1 is not a valid value */
#define EMPTY_PSM_VERSION   -1


/*****************************************************************************
 * Prototypes
 *****************************************************************************/
void input_ParsePES  ( struct input_thread_s *, struct es_descriptor_s * );
void input_GatherPES ( struct input_thread_s *, struct data_packet_s *,
                       struct es_descriptor_s *, boolean_t, boolean_t );
es_descriptor_t * input_ParsePS( struct input_thread_s *,
                                 struct data_packet_s * );
void input_DemuxPS   ( struct input_thread_s *, struct data_packet_s * );
void input_DemuxTS   ( struct input_thread_s *, struct data_packet_s * );
void input_DemuxPSI  ( struct input_thread_s *, struct data_packet_s *,
                       struct es_descriptor_s *, boolean_t, boolean_t );

