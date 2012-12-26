
/* Copyright Nate Carson 2012 */

\set ON_ERROR_STOP

set DYNAMIC_LIBRARY_PATH to 
	'$libdir'
	':/home/muskrat/src/chess/trunk/src/build/lib'
;

--------------------------------------
--	domains --------------------------
--------------------------------------


drop domain if exists san cascade;
create domain san as text
	--TODO
	check (1=1)
;

-- Fen-like string without 50 move and full move fields
drop domain if exists board cascade;
create domain board as text
	check (
		value ~ '^([pnbrqkPNBRQK1-8]|/|)+ [wb]{1} ([KkQq]{1,4}|-) ([a-h][1-8]|-)$'
	)
;


-- Real fen string with the 50 move and full move fields
drop domain if exists fen cascade;
create domain fen as text
	check 
	(
		value ~ '^([pnbrqkPNBRQK1-8]|/|)+ [wb]{1} ([KkQq]{1,4}|-) ([a-h][1-8]|-) \d+ \d+$'
	)
;

create or replace function tofen(board) returns fen language sql immutable as
	$$
		select ($1 || ' 0 1')::fen
	$$
;

-- lossy - cuts last two fields
create or replace function toboard(fen) returns board language sql immutable as
	$$
		select string_agg(a, ' ')::board
		from (
			select 
			regexp_split_to_table($1, ' ') as a 
			limit 4
		)t
	$$
;

--------------------------------------
--	enums  ---------------------------
--------------------------------------

drop type if exists gameresult cascade;
create type gameresult as enum
(
	'1-0', '1/2-1/2', '0-1', '*'
);


--------------------------------------
--	piece   --------------------------
--------------------------------------


drop type if exists piece cascade;
create type piece ;

create or replace function piece_in(cstring) returns piece
	as 	'libpgchess', 'piece_in' language C strict
;

create or replace function piece_out(piece) returns cstring
	as 	'libpgchess', 'piece_out' language C strict
;

create type piece(
	internallength = 1,
	input = piece_in,
	output = piece_out 
	,PASSEDBYVALUE
	,alignment = char
);
--create cast (piece as numeric) without function;


--------------------------------------
--	square  -----------------------
--------------------------------------


drop type if exists square cascade;
create type square;

create or replace function square_in(cstring) returns square
	as 	'libpgchess', 'square_in' language C strict
;

create or replace function square_out(square) returns cstring
	as 	'libpgchess', 'square_out' language C strict
;

create type square(
	internallength = 1,
	input = square_in,
	output = square_out
	,PASSEDBYVALUE
	,alignment = char
);


--------------------------------------
--	piecesquare  ---------------------
--------------------------------------


drop type if exists piecesquare cascade;
create type piecesquare;

create or replace function piecesquare_in(cstring) returns piecesquare
	as 	'libpgchess', 'piecesquare_in' language C strict
;

create or replace function piecesquare_out(piecesquare) returns cstring
	as 	'libpgchess', 'piecesquare_out' language C strict
;

create type piecesquare (
	internallength = 2,
	input = piecesquare_in,
	output = piecesquare_out
	,PASSEDBYVALUE
	,alignment = int2
);
--create cast (piecesquare as int2) without function;


create or replace function topiece(piecesquare) returns piece
	as 	'libpgchess', 'piecesquare_piece' language C strict
;
create cast (piecesquare as piece) with function topiece(piecesquare);

create or replace function tosquare(piecesquare) returns square 
	as 	'libpgchess', 'piecesquare_square' language C strict
;
create cast (piecesquare as square) with function tosquare(piecesquare);


--------------------------------------
--	move  ----------------------------
--------------------------------------


drop type if exists move cascade;
create type move;

create or replace function move_in(cstring) returns move 
	as 	'libpgchess', 'move_in' language C strict
;

create or replace function move_out(move) returns cstring
	as 	'libpgchess', 'move_out' language C strict
;

create type move(
	internallength = 2
	,input = move_in
	,output = move_out 
	,like = int2
);

create or replace function move_cmp(move, move) returns integer
	as 	'libpgchess', 'chess_cmp' language C strict
;

create or replace function move_eq(move, move) returns boolean
	as 	'libpgchess', 'chess_cmp_eq' language C strict
;

create or replace function move_neq(move, move) returns boolean
	as 	'libpgchess', 'chess_cmp_neq' language C strict
;

create or replace function move_lt(move, move) returns boolean
	as 	'libpgchess', 'chess_cmp_lt' language C strict
;

create or replace function move_gt(move, move) returns boolean
	as 	'libpgchess', 'chess_cmp_gt' language C strict
;

create or replace function move_gteq(move, move) returns boolean
	as 	'libpgchess', 'chess_cmp_gteq' language C strict
;

create or replace function move_lteq(move, move) returns boolean
	as 	'libpgchess', 'chess_cmp_lteq' language C strict
;

create operator = (
	leftarg = move, rightarg = move, procedure = move_eq,
	commutator = = , negator = <> ,
	restrict = eqsel, join = eqjoinsel
);

create operator <> (
	leftarg = move, rightarg = move, procedure = move_neq,
	commutator = <> , negator = = ,
	restrict = neqsel, join = neqjoinsel
);

create operator < (
   leftarg = move, rightarg = move, procedure = move_lt,
   commutator = > , negator = >= ,
   restrict = scalarltsel, join = scalarltjoinsel
);
CREATE OPERATOR <= (
   leftarg = move, rightarg = move, procedure = move_lteq,
   commutator = >= , negator = > ,
   restrict = scalarltsel, join = scalarltjoinsel
);
CREATE OPERATOR >= (
   leftarg = move, rightarg = move, procedure = move_gteq,
   commutator = <= , negator = < ,
   restrict = scalargtsel, join = scalargtjoinsel
);
CREATE OPERATOR > (
   leftarg = move, rightarg = move, procedure = move_gt,
   commutator = < , negator = <= ,
   restrict = scalargtsel, join = scalargtjoinsel
);


create operator class move_ops
    default for type move using btree as
        operator        1       < ,
        operator        2       <= ,
        operator        3       = ,
        operator        4       >= ,
        operator        5       > ,
        function        1       move_cmp(move, move)
;
create operator class move_ops
    default for type move using hash as
        operator        1       =
;

create or replace function move_from(move) returns square
	as 	'libpgchess', 'move_from' language C strict
;

create or replace function move_to(move) returns square
	as 	'libpgchess', 'move_to' language C strict
;


--------------------------------------
--	position  ------------------------
--------------------------------------


create or replace function position_test(fen) returns text
	as 	'libpgchess', 'position_test'
	language C strict
;

create or replace function position_pieces(fen) returns piecesquare[]
	as 	'libpgchess', 'position_pieces' language C strict
;

create or replace function position_piece(fen, square) returns piece
	as 	'libpgchess', 'position_piece' language C strict
;

create or replace function position_moves(fen) returns move[]
	as 	'libpgchess', 'position_moves' language C strict
;

create or replace function position_attacks_from(fen, square) returns square[]
	as 	'libpgchess', 'position_attacks_from' language C strict
;

create or replace function position_attacked_by(fen, square) returns piecesquare[]
	as 	'libpgchess', 'position_attacked_by' language C strict
;

-- XXX this checks nothing -- not even if there is a piece on the from square
create or replace function position_make_move(fen, move) returns text 
	as 	'libpgchess', 'position_make_move' language C strict
;

create or replace function position_moves_san(fen) returns text[]
	as 	'libpgchess', 'position_moves_san' language C strict
;

create or replace function position_move_san(fen, move) returns text
	as 	'libpgchess', 'position_move_san' language C strict
;

/*

create or replace function position_material(fen, color) returns text 
	as 	'libpgchess', 'position_material' language C strict
;

create or replace function position_score(fen, depth integer) returns integer
	as 	'libpgchess', 'position_score' language C strict
;


*/
