
/* Copyright Nate Carson 2012 */

#include <stddef.h>
#include <stdio.h>

/* chesslib */
#include "chess.h"
#include "position.h"
#include "fen.h"
#include "generate.h"
#include "print.h"


/* pg */
#include "postgres.h"
#include "fmgr.h"
#include "utils/builtins.h"

#include "utils/array.h"
#include "utils/lsyscache.h"
#include "catalog/namespace.h"
#include "catalog/pg_type.h"


// FIXME validate make_move and move_san (sometimes crashing)



/*
 * 1 Q 1 0
 * 2 R 2 1 0
 * 3 M 4 3 2 1 0 -- minor pieces
 * 4 P 8 7 6 5 4 3 2 1 0
 * 10 bits
 *
 * QR BBN PPP
 * QRRBBN.PPP
1 2 4 8 | 16 32 64 | 128 256 | 512
 */


const int	MAX_FEN = 100;
const int	MAX_PIECES = 32;
const int	MAX_MOVES = 192;
const int	MAX_SAN = 10;

// postgresql type names
const char TYPE_PIECESQUARE[]  = "piecesquare";
const char TYPE_MOVE[] = "move";
const char TYPE_SQUARE[] = "square";
const char TYPE_PIECE[] = "piece";


#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

#define NOTICE1(msg) ereport(NOTICE, (errcode(ERRCODE_INTERNAL_ERROR), errmsg("%s", msg)))
#define NOTICE2(msg, arg) ereport(NOTICE, (errcode(ERRCODE_INTERNAL_ERROR), errmsg(msg, arg)))
#define NOTICE3(msg, arg1, arg2) ereport(NOTICE, (errcode(ERRCODE_INTERNAL_ERROR), errmsg(msg, arg1, arg2)))

#define BAD_TYPE_IN(type, input) ereport( \
			ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), \
			errmsg("invalid input syntax for %s: \"%s\"", type, input)))

#define BAD_TYPE_OUT(type, input) ereport( \
			ERROR, (errcode(ERRCODE_DATA_CORRUPTED), \
			errmsg("corrupt internal data for %s: \"%d\"", type, input)))


/*

chess=# select to_hex(idx), idx::bit(4)  from generate_series(0, 15) as idx;
 to_hex | idx  
--------+------
 0	  | 0000
 1	  | 0001
 2	  | 0010
 3	  | 0011
 4	  | 0100
 5	  | 0101
 6	  | 0110
 7	  | 0111
 8	  | 1000
 9	  | 1001
 a	  | 1010
 b	  | 1011
 c	  | 1100
 d	  | 1101
 e	  | 1110
 f	  | 1111
(16 rows)


*/

PG_FUNCTION_INFO_V1(chess_cmp);
PG_FUNCTION_INFO_V1(chess_cmp_eq);
PG_FUNCTION_INFO_V1(chess_cmp_neq);
PG_FUNCTION_INFO_V1(chess_cmp_gt);
PG_FUNCTION_INFO_V1(chess_cmp_lt);
PG_FUNCTION_INFO_V1(chess_cmp_lteq);
PG_FUNCTION_INFO_V1(chess_cmp_gteq);

PG_FUNCTION_INFO_V1(position_test);
PG_FUNCTION_INFO_V1(position_score);
PG_FUNCTION_INFO_V1(position_pieces);
PG_FUNCTION_INFO_V1(position_piece);
PG_FUNCTION_INFO_V1(position_attacked_by);
PG_FUNCTION_INFO_V1(position_attacks_from);
PG_FUNCTION_INFO_V1(position_moves);
PG_FUNCTION_INFO_V1(position_moves_san);
PG_FUNCTION_INFO_V1(position_move_san);
PG_FUNCTION_INFO_V1(position_material);
PG_FUNCTION_INFO_V1(position_make_move);

PG_FUNCTION_INFO_V1(piecesquare_in);
PG_FUNCTION_INFO_V1(piecesquare_out);
PG_FUNCTION_INFO_V1(piecesquare_square);
PG_FUNCTION_INFO_V1(piecesquare_piece);

PG_FUNCTION_INFO_V1(move_in);
PG_FUNCTION_INFO_V1(move_out);
PG_FUNCTION_INFO_V1(move_from);
PG_FUNCTION_INFO_V1(move_to);

PG_FUNCTION_INFO_V1(square_in);
PG_FUNCTION_INFO_V1(square_out);

PG_FUNCTION_INFO_V1(piece_in);
PG_FUNCTION_INFO_V1(piece_out);


/********************************************************
* 		util
********************************************************/



static ArrayType * make_array(const char *typname, size_t size, Datum * data)
{
	ArrayType	*result;
	Oid			element_type = TypenameGetTypid(typname);
	if (!OidIsValid(element_type))
		elog(ERROR, "could not find '%s' type.", typname);

	int16		typlen;
	bool		typbyval;
	char		typalign;

	get_typlenbyvalalign(element_type, &typlen, &typbyval, &typalign);

	result = construct_array(data, size, element_type, typlen, typbyval, typalign);
	if (!result)
		elog(ERROR, "constructing array failed");
	return result;
}

/********************************************************
* 		operators
********************************************************/

static int
_cmp_internal(int a, int b)
{
	// Our biggest type is int16 so we should be able to promote
	// and use this function for everything.
	
    if (a < b)
        return -1;
    if (a > b)
        return 1;
    return 0;
}

Datum
chess_cmp(PG_FUNCTION_ARGS)
{
    int			a = (int) PG_GETARG_INT32(0);
    int			b = (int) PG_GETARG_INT32(1);

    PG_RETURN_INT32(_cmp_internal(a, b));
}

Datum
chess_cmp_eq(PG_FUNCTION_ARGS)
{
    int			a = (int) PG_GETARG_INT32(0);
    int			b = (int) PG_GETARG_INT32(1);

    PG_RETURN_BOOL(_cmp_internal(a, b) == 0);
}

Datum
chess_cmp_neq(PG_FUNCTION_ARGS)
{
    int			a = (int) PG_GETARG_INT32(0);
    int			b = (int) PG_GETARG_INT32(1);

    PG_RETURN_BOOL(_cmp_internal(a, b) != 0);
}

Datum
chess_cmp_lt(PG_FUNCTION_ARGS)
{
    int			a = (int) PG_GETARG_INT32(0);
    int			b = (int) PG_GETARG_INT32(1);

    PG_RETURN_BOOL(_cmp_internal(a, b) < 0);
}

Datum
chess_cmp_gt(PG_FUNCTION_ARGS)
{
    int			a = (int) PG_GETARG_INT32(0);
    int			b = (int) PG_GETARG_INT32(1);

    PG_RETURN_BOOL(_cmp_internal(a, b) > 0);
}

Datum
chess_cmp_lteq(PG_FUNCTION_ARGS)
{
    int			a = (int) PG_GETARG_INT32(0);
    int			b = (int) PG_GETARG_INT32(1);

    PG_RETURN_BOOL(_cmp_internal(a, b) <= 0);
}

Datum
chess_cmp_gteq(PG_FUNCTION_ARGS)
{
    int			a = (int) PG_GETARG_INT32(0);
    int			b = (int) PG_GETARG_INT32(1);

    PG_RETURN_BOOL(_cmp_internal(a, b) >= 0);
}




/********************************************************
* 		piece
********************************************************/

static ChessPiece _piece_in(char * str)
{
	ChessPiece		piece = chess_piece_from_char(str[0]);
	if (piece == CHESS_PIECE_NONE)
		BAD_TYPE_IN(TYPE_PIECE, str);
	return piece;
}

static char	_piece_out(ChessPiece piece)
{
    if (!(piece >= CHESS_PIECE_WHITE_PAWN && piece <= CHESS_PIECE_BLACK_KING))
		BAD_TYPE_OUT(TYPE_PIECE, piece);
	return chess_piece_to_char(piece);
}


Datum
piece_in(PG_FUNCTION_ARGS)
{
	char 			*str = PG_GETARG_CSTRING(0);

	if (strlen(str) != 1)
		BAD_TYPE_IN(TYPE_PIECE, str);

	ChessPiece		piece = _piece_in(str);

	PG_RETURN_CHAR((char)piece);
}


Datum
piece_out(PG_FUNCTION_ARGS)
{
	ChessPiece		piece = (ChessPiece)PG_GETARG_CHAR(0);
	char			*result = (char *) palloc(2);

	result[0] = _piece_out(piece);
	result[1] = '\0';

	PG_RETURN_CSTRING(result);
}


/********************************************************
* 		square
********************************************************/

static ChessSquare _square_in(char *str)
{
	ChessSquare		square;
	ChessFile		file = chess_file_from_char(str[0]);
	ChessRank		rank = chess_rank_from_char(str[1]);

	if (file == CHESS_FILE_INVALID || rank == CHESS_RANK_INVALID)
		BAD_TYPE_IN(TYPE_SQUARE, str);

	square = chess_square_from_fr(file, rank);

	if (square == CHESS_SQUARE_INVALID)
		BAD_TYPE_IN(TYPE_SQUARE, str);

	return square;
}

static void _square_out(const ChessSquare square, char * str)
{
	ChessFile		file = chess_square_file(square);
	ChessRank		rank = chess_square_rank(square);

	if (file == CHESS_FILE_INVALID || rank == CHESS_RANK_INVALID)
		BAD_TYPE_OUT(TYPE_SQUARE, square);

	str[0] = chess_file_to_char(file);
	str[1] = chess_rank_to_char(rank);
}


Datum
square_in(PG_FUNCTION_ARGS)
{
	ChessSquare		square;
	char 			*str = PG_GETARG_CSTRING(0);

	if (strlen(str) != 2)
		BAD_TYPE_IN(TYPE_SQUARE, str);
	
	square = _square_in(str);

	PG_RETURN_CHAR((char)square);
}


Datum
square_out(PG_FUNCTION_ARGS)
{
	ChessSquare		square = (ChessSquare)PG_GETARG_CHAR(0);
	char			*result = (char *) palloc(3);

	_square_out(square, result);
	result[2] = '\0';

	PG_RETURN_CSTRING(result);
}


/********************************************************
* 		piecesquare
********************************************************/

static uint16 _piecesquare_in(ChessPiece piece, ChessSquare square)
{
	uint16			result =0;
	result = piece << 6;
	result = result | square;
	return result;
}

static ChessPiece _piecesquare_piece(uint16 piecesquare)
{
	ChessPiece		piece = piecesquare >> 6 & 15;

    if (!(piece >= CHESS_PIECE_WHITE_PAWN && piece <= CHESS_PIECE_BLACK_KING))
		BAD_TYPE_OUT(TYPE_PIECE, piece);
	return piece;
}

static ChessPiece _piecesquare_square(uint16 piecesquare)
{
	return piecesquare & 63;
}



Datum
piecesquare_in(PG_FUNCTION_ARGS)
{
	char 			*str = PG_GETARG_CSTRING(0);
	ChessPiece		piece;
	ChessSquare		square;
	
	if (strlen(str) != 3)
		BAD_TYPE_IN(TYPE_PIECE, str);
	
	piece = _piece_in(str);
	square = _square_in(++str);
	
	PG_RETURN_INT16(_piecesquare_in(piece, square));
}


Datum
piecesquare_out(PG_FUNCTION_ARGS)
{
	uint16			piecesquare = PG_GETARG_UINT16(0);
	char			*result = (char *) palloc(4);
	ChessPiece		piece = _piecesquare_piece(piecesquare);
	ChessSquare		square = _piecesquare_square(piecesquare);

	result[0] = _piece_out(piece);
	_square_out(square, result+1);
	result[3] = '\0';

	PG_RETURN_CSTRING(result);
}


Datum
piecesquare_square(PG_FUNCTION_ARGS)
{
	uint16			piecesquare = PG_GETARG_UINT16(0);
	PG_RETURN_CHAR(_piecesquare_square(piecesquare));
}


Datum
piecesquare_piece(PG_FUNCTION_ARGS)
{
	uint16			piecesquare = PG_GETARG_UINT16(0);
	PG_RETURN_CHAR(_piecesquare_piece(piecesquare));
}



/********************************************************
* 		move
********************************************************/

static void _move_out(ChessMove move, char * result)
{

	ChessSquare		from = chess_move_from(move);
	ChessSquare		to = chess_move_to(move);
	ChessMovePromote promote = chess_move_promotes(move);

	if (!(promote >= CHESS_MOVE_PROMOTE_NONE && promote <= CHESS_MOVE_PROMOTE_QUEEN))
		BAD_TYPE_OUT(TYPE_MOVE, move);

	_square_out(from, result);
	_square_out(to, result+2);

	if (promote != CHESS_MOVE_PROMOTE_NONE)
	{
		result[4] = chess_move_promote_to_char(promote);
		result[5] = '\0';
	}
	else
		result[4] = '\0';
}


Datum
move_in(PG_FUNCTION_ARGS)
{
	char 			*str = PG_GETARG_CSTRING(0);
	size_t			len = strlen(str);
	ChessMove		result;
	ChessMovePromote promote = CHESS_MOVE_PROMOTE_NONE;

	if (len != 4 && len != 5)
		BAD_TYPE_IN(TYPE_MOVE, str);

	ChessSquare 	from = _square_in(str);
	ChessSquare 	to = _square_in(str+2);

	if (len == 5)
	{
		promote = chess_move_promote_from_char(str[4]);
		if (promote == CHESS_MOVE_PROMOTE_NONE)
			BAD_TYPE_IN(TYPE_MOVE, str);
	}

	result = chess_move_make_promote(from, to, promote);
	PG_RETURN_INT16(result);
}

Datum
move_out(PG_FUNCTION_ARGS)
{
	ChessMove		move = (ChessMove)PG_GETARG_INT16(0);
	char			*result = (char *) palloc(6);

	_move_out(move, result);

	PG_RETURN_CSTRING(result);
}


Datum
move_from(PG_FUNCTION_ARGS)
{
	ChessMove		move = PG_GETARG_INT16(0);
	PG_RETURN_CHAR(chess_move_from(move));
}

Datum
move_to(PG_FUNCTION_ARGS)
{
	ChessMove		move = PG_GETARG_INT16(0);
	PG_RETURN_CHAR(chess_move_to(move));
}

/*
 * FIXME need piececlass for return
Datum
move_promote(PG_FUNCTION_ARGS)
{
	ChessMove		move = PG_GETARG_INT16(0);
	PG_RETURN_CHAR(chess_move_to(move));
}
*/


/*******************************************************
* 		position
********************************************************/

// FIXME figure out if generate moves generates different promotions
static bool is_move_legal(ChessPosition * pos, ChessMove move)
{
    ChessArray 		moves;
	ChessMove 		test_move;
	size_t			idx;
	bool 			is_legal = false;

    chess_generate_init();
    chess_array_init(&moves, sizeof(ChessMove));
	chess_generate_moves(pos, &moves);

    for (idx = 0; idx < moves.size; idx++)
	{
		test_move = *((ChessMove *)chess_array_elem(&moves, idx));
		if (test_move == move)
			is_legal = true;
	}
    chess_array_cleanup(&moves);
	return is_legal;
}


Datum
position_test(PG_FUNCTION_ARGS)
{

	ChessPosition	*pos = chess_position_new_fen((char*)VARDATA(PG_GETARG_TEXT_P(0)));
	char			new_fen[MAX_FEN];

	chess_fen_save(pos, new_fen);

	chess_position_destroy(pos);
	PG_RETURN_TEXT_P(CStringGetTextDatum(new_fen));
}

Datum
position_pieces(PG_FUNCTION_ARGS)
{
	ChessPosition	*pos = chess_position_new_fen((char*)VARDATA(PG_GETARG_TEXT_P(0)));
	Datum 			*d = (Datum *) palloc(sizeof(Datum) * MAX_PIECES + 1);
	size_t			idx = 0;
	ChessSquare		sq;
	ChessPiece		piece;

    for (sq = CHESS_SQUARE_A1; sq <= CHESS_SQUARE_H8; sq++)
	{
		piece = chess_position_piece(pos, sq);
		if (piece != CHESS_PIECE_NONE)
			d[idx++] = (Datum)_piecesquare_in(piece, sq);
			
	}
	chess_position_destroy(pos);
	PG_RETURN_ARRAYTYPE_P(make_array(TYPE_PIECESQUARE, idx, d));
}

Datum
position_piece(PG_FUNCTION_ARGS)
{

	ChessPosition	*pos = chess_position_new_fen((char*)VARDATA(PG_GETARG_TEXT_P(0)));
	ChessPiece		piece = chess_position_piece(pos, PG_GETARG_CHAR(1));

	chess_position_destroy(pos);

	if (piece == CHESS_PIECE_NONE)
		PG_RETURN_NULL();
	PG_RETURN_CHAR(piece);
}


Datum
position_moves(PG_FUNCTION_ARGS)
{
	ChessPosition	*pos = chess_position_new_fen((char*)VARDATA(PG_GETARG_TEXT_P(0)));
	Datum 			*d = (Datum *) palloc(sizeof(Datum) * MAX_MOVES);
    ChessArray 		moves;
	size_t			idx;

    chess_generate_init();
    chess_array_init(&moves, sizeof(ChessMove));
	chess_generate_moves(pos, &moves);

    for (idx = 0; idx < moves.size; idx++)
	{
		d[idx] = *((Datum *)chess_array_elem(&moves, idx));
	}

    chess_array_cleanup(&moves);
	chess_position_destroy(pos);

	PG_RETURN_ARRAYTYPE_P(make_array(TYPE_MOVE, idx, d));
}

Datum
position_moves_san(PG_FUNCTION_ARGS)
{
	ChessPosition	*pos = chess_position_new_fen((char*)VARDATA(PG_GETARG_TEXT_P(0)));
	Datum 			*d = (Datum *) palloc(sizeof(Datum) * MAX_MOVES);
    ChessArray 		moves;
	ChessMove		move;
	char			*san;
	size_t			idx;

    chess_generate_init();
    chess_array_init(&moves, sizeof(ChessMove));
	chess_generate_moves(pos, &moves);

    for (idx = 0; idx < moves.size; idx++)
	{
		move = *(ChessMove *)chess_array_elem(&moves, idx);
		san = (char *)palloc(MAX_SAN);
		chess_print_move_san(move, pos, san);
		d[idx] = CStringGetTextDatum(san);
	}

    chess_array_cleanup(&moves);
	chess_position_destroy(pos);

	PG_RETURN_ARRAYTYPE_P(make_array("text", idx, d));

}

Datum
position_move_san(PG_FUNCTION_ARGS)
{
	ChessPosition	*pos = chess_position_new_fen((char*)VARDATA(PG_GETARG_TEXT_P(0)));
	ChessMove		move = (ChessMove)PG_GETARG_INT16(1);
	char			*san = (char *)palloc(MAX_SAN);

	if (is_move_legal(pos, move))
		chess_print_move_san(move, pos, san);
	else
		PG_RETURN_NULL();

	chess_position_destroy(pos);
	PG_RETURN_TEXT_P(CStringGetTextDatum(san));
}


// should be passed in from generated moves
static bool is_attacking(ChessPosition * pos, ChessMove move)
{
	ChessSquare		from = chess_move_from(move);
	ChessSquare		to = chess_move_to(move);
	ChessPiece		subject = chess_position_piece(pos, from);
	ChessPiece		target = chess_position_piece(pos, to);

	if (
		subject == CHESS_PIECE_NONE
		|| target == CHESS_PIECE_NONE
		|| (chess_piece_color(subject) == chess_piece_color(target))
	)
		return false;
	return true;
}

Datum
position_attacks_from(PG_FUNCTION_ARGS)
{
	ChessPosition	*pos = chess_position_new_fen((char*)VARDATA(PG_GETARG_TEXT_P(0)));
	ChessSquare		subject = PG_GETARG_CHAR(1);
	ChessPiece		piece = chess_position_piece(pos, subject);
	Datum 			*d = (Datum *) palloc(sizeof(Datum) * MAX_MOVES);
    ChessArray 		moves;
    ChessMove		move;
	size_t			idx, elements = 0;

	// if there is no piece there there is no attacks
	if (piece == CHESS_PIECE_NONE)
		PG_RETURN_NULL();

    chess_generate_init();
    chess_array_init(&moves, sizeof(ChessMove));
	chess_generate_moves(pos, &moves);


    for (idx = 0; idx < moves.size; idx++)
	{
		move = *((ChessMove *)chess_array_elem(&moves, idx));
		if (chess_move_from(move) == subject && is_attacking(pos, move))
			d[elements++] = (Datum) chess_move_to(move);
	}

    chess_array_cleanup(&moves);
	chess_position_destroy(pos);

	PG_RETURN_ARRAYTYPE_P(make_array(TYPE_SQUARE, elements, d));
}

Datum
position_attacked_by(PG_FUNCTION_ARGS)
{
	ChessPosition	*pos = chess_position_new_fen((char*)VARDATA(PG_GETARG_TEXT_P(0)));
	ChessSquare		from, target = PG_GETARG_CHAR(1);
	ChessPiece		piece = chess_position_piece(pos, target);
	Datum 			*d = (Datum *) palloc(sizeof(Datum) * MAX_MOVES);
    ChessArray 		moves;
    ChessMove		move;
	size_t			idx, elements = 0;

	// if there is no piece there there are no attacks
	if (piece == CHESS_PIECE_NONE)
		PG_RETURN_NULL();

    chess_generate_init();
    chess_array_init(&moves, sizeof(ChessMove));
	chess_generate_moves(pos, &moves);


    for (idx = 0; idx < moves.size; idx++)
	{
		move = *((ChessMove *)chess_array_elem(&moves, idx));
		from = chess_move_from(move);
		if (chess_move_to(move) == target && is_attacking(pos, move))
			d[elements++] = (Datum) _piecesquare_in(chess_position_piece(pos, from), from);
	}

    chess_array_cleanup(&moves);
	chess_position_destroy(pos);

	PG_RETURN_ARRAYTYPE_P(make_array(TYPE_PIECESQUARE, elements, d));
}


Datum
position_make_move(PG_FUNCTION_ARGS)
{

	ChessPosition	*pos = chess_position_new_fen((char*)VARDATA(PG_GETARG_TEXT_P(0)));
	ChessMove		move = (ChessMove)PG_GETARG_INT16(1);
	char			new_fen[MAX_FEN];
	
	if (is_move_legal(pos, move))
		chess_position_make_move(pos, move);
	else
		PG_RETURN_NULL();

	chess_fen_save(pos, new_fen);

	chess_position_destroy(pos);
	PG_RETURN_TEXT_P(CStringGetTextDatum(new_fen));
}


