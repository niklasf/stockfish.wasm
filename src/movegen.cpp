/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2020 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <cassert>

#include "movegen.h"
#include "position.h"

namespace {

  template<Variant V, GenType Type, Direction D>
  ExtMove* make_promotions(ExtMove* moveList, Square to, Square ksq) {

#ifdef ANTI
    if (V == ANTI_VARIANT)
    {
        if (Type == QUIETS || Type == CAPTURES || Type == NON_EVASIONS)
        {
            *moveList++ = make<PROMOTION>(to - D, to, QUEEN);
            *moveList++ = make<PROMOTION>(to - D, to, ROOK);
            *moveList++ = make<PROMOTION>(to - D, to, BISHOP);
            *moveList++ = make<PROMOTION>(to - D, to, KNIGHT);
            *moveList++ = make<PROMOTION>(to - D, to, KING);
        }
        return moveList;
    }
#endif
#ifdef LOSERS
    if (V == LOSERS_VARIANT)
    {
        if (Type == QUIETS || Type == CAPTURES || Type == EVASIONS || Type == NON_EVASIONS)
        {
            *moveList++ = make<PROMOTION>(to - D, to, QUEEN);
            *moveList++ = make<PROMOTION>(to - D, to, ROOK);
            *moveList++ = make<PROMOTION>(to - D, to, BISHOP);
            *moveList++ = make<PROMOTION>(to - D, to, KNIGHT);
        }
        return moveList;
    }
#endif
    if (Type == CAPTURES || Type == EVASIONS || Type == NON_EVASIONS)
        *moveList++ = make<PROMOTION>(to - D, to, QUEEN);

    if (Type == QUIETS || Type == EVASIONS || Type == NON_EVASIONS)
    {
        *moveList++ = make<PROMOTION>(to - D, to, ROOK);
        *moveList++ = make<PROMOTION>(to - D, to, BISHOP);
        *moveList++ = make<PROMOTION>(to - D, to, KNIGHT);
#ifdef EXTINCTION
        if (V == EXTINCTION_VARIANT)
            *moveList++ = make<PROMOTION>(to - D, to, KING);
#endif
    }

    // Knight promotion is the only promotion that can give a direct check
    // that's not already included in the queen promotion.
#ifdef HORDE
    if (V == HORDE_VARIANT && ksq == SQ_NONE) {} else
#endif
    if (Type == QUIET_CHECKS && (PseudoAttacks[KNIGHT][to] & ksq))
        *moveList++ = make<PROMOTION>(to - D, to, KNIGHT);
    else
        (void)ksq; // Silence a warning under MSVC

    return moveList;
  }

#ifdef CRAZYHOUSE
  template<Color Us, PieceType Pt, bool Checks>
  ExtMove* generate_drops(const Position& pos, ExtMove* moveList, Bitboard b) {
    if (pos.count_in_hand<Pt>(Us))
    {
#ifdef PLACEMENT
        if (pos.is_placement() && pos.count_in_hand<BISHOP>(Us))
        {
            if (Pt == BISHOP)
            {
                if (pos.pieces(Us, BISHOP) & DarkSquares)
                    b &= ~DarkSquares;
                if (pos.pieces(Us, BISHOP) & ~DarkSquares)
                    b &= DarkSquares;
            }
            else
            {
                if (!(pos.pieces(Us, BISHOP) & DarkSquares) && popcount((b & DarkSquares)) <= 1)
                    b &= ~DarkSquares;
                if (!(pos.pieces(Us, BISHOP) & ~DarkSquares) && popcount((b & ~DarkSquares)) <= 1)
                    b &= DarkSquares;
            }
        }
#endif
        if (Checks)
            b &= pos.check_squares(Pt);
        while (b)
            *moveList++ = make_drop(pop_lsb(&b), make_piece(Us, Pt));
    }

    return moveList;
  }
#endif

#if defined(ANTI) || defined(EXTINCTION) || defined(TWOKINGS)
  template<Variant V, Color Us, GenType Type>
  ExtMove* generate_king_moves(const Position& pos, ExtMove* moveList, Bitboard target) {
    Bitboard kings = pos.pieces(Us, KING);
    while (kings)
    {
        Square ksq = pop_lsb(&kings);
        Bitboard b = pos.attacks_from<KING>(ksq) & target;
        while (b)
            *moveList++ = make_move(ksq, pop_lsb(&b));
    }
    return moveList;
  }
#endif

  template<Variant V, Color Us, GenType Type>
  ExtMove* generate_pawn_moves(const Position& pos, ExtMove* moveList, Bitboard target) {

    constexpr Color     Them     = (Us == WHITE ? BLACK      : WHITE);
    constexpr Bitboard  TRank7BB = (Us == WHITE ? Rank7BB    : Rank2BB);
#ifdef HORDE
    constexpr Bitboard  TRank2BB = (Us == WHITE ? Rank2BB    : Rank7BB);
#endif
    constexpr Bitboard  TRank3BB = (Us == WHITE ? Rank3BB    : Rank6BB);
    constexpr Direction Up       = pawn_push(Us);
    constexpr Direction UpRight  = (Us == WHITE ? NORTH_EAST : SOUTH_WEST);
    constexpr Direction UpLeft   = (Us == WHITE ? NORTH_WEST : SOUTH_EAST);

    Square ksq;
#ifdef HORDE
    if (V == HORDE_VARIANT && pos.is_horde_color(Them))
        ksq = SQ_NONE;
    else
#endif
    ksq = pos.square<KING>(Them);
    Bitboard emptySquares;

    Bitboard pawnsOn7    = pos.pieces(Us, PAWN) &  TRank7BB;
    Bitboard pawnsNotOn7 = pos.pieces(Us, PAWN) & ~TRank7BB;

    Bitboard enemies = (Type == EVASIONS ? pos.pieces(Them) & target:
                        Type == CAPTURES ? target : pos.pieces(Them));
#ifdef ATOMIC
    if (V == ATOMIC_VARIANT)
        enemies &= (Type == CAPTURES || Type == NON_EVASIONS) ? target : ~adjacent_squares_bb(pos.pieces(Us, KING));
#endif

    // Single and double pawn pushes, no promotions
    if (Type != CAPTURES)
    {
        emptySquares = (Type == QUIETS || Type == QUIET_CHECKS ? target : ~pos.pieces());
#ifdef ANTI
        if (V == ANTI_VARIANT)
            emptySquares &= target;
#endif

        Bitboard b1 = shift<Up>(pawnsNotOn7)   & emptySquares;
        Bitboard b2 = shift<Up>(b1 & TRank3BB) & emptySquares;
#ifdef HORDE
        if (V == HORDE_VARIANT)
            b2 = shift<Up>(b1 & (TRank2BB | TRank3BB)) & emptySquares;
#endif

#ifdef LOSERS
        if (V == LOSERS_VARIANT)
        {
            b1 &= target;
            b2 &= target;
        }
#endif
        if (Type == EVASIONS) // Consider only blocking squares
        {
            b1 &= target;
            b2 &= target;
        }

        if (Type == QUIET_CHECKS)
        {
            b1 &= pos.attacks_from<PAWN>(ksq, Them);
            b2 &= pos.attacks_from<PAWN>(ksq, Them);

            // Add pawn pushes which give discovered check. This is possible only
            // if the pawn is not on the same file as the enemy king, because we
            // don't generate captures. Note that a possible discovery check
            // promotion has been already generated amongst the captures.
            Bitboard dcCandidateQuiets = pos.blockers_for_king(Them) & pawnsNotOn7;
            if (dcCandidateQuiets)
            {
                Bitboard dc1 = shift<Up>(dcCandidateQuiets) & emptySquares & ~file_bb(ksq);
                Bitboard dc2 = shift<Up>(dc1 & TRank3BB) & emptySquares;

                b1 |= dc1;
                b2 |= dc2;
            }
        }

        while (b1)
        {
            Square to = pop_lsb(&b1);
            *moveList++ = make_move(to - Up, to);
        }

        while (b2)
        {
            Square to = pop_lsb(&b2);
            *moveList++ = make_move(to - Up - Up, to);
        }
    }

    // Promotions and underpromotions
    if (pawnsOn7)
    {
        if (Type == CAPTURES)
        {
            emptySquares = ~pos.pieces();
#ifdef ATOMIC
            // Promotes only if promotion wins or explodes checkers
            if (V == ATOMIC_VARIANT && pos.checkers())
                emptySquares &= target;
#endif
        }
#ifdef ANTI
        if (V == ANTI_VARIANT)
            emptySquares &= target;
#endif
#ifdef LOSERS
        if (V == LOSERS_VARIANT)
            emptySquares &= target;
#endif

        if (Type == EVASIONS)
            emptySquares &= target;

        Bitboard b1 = shift<UpRight>(pawnsOn7) & enemies;
        Bitboard b2 = shift<UpLeft >(pawnsOn7) & enemies;
        Bitboard b3 = shift<Up     >(pawnsOn7) & emptySquares;

        while (b1)
            moveList = make_promotions<V, Type, UpRight>(moveList, pop_lsb(&b1), ksq);

        while (b2)
            moveList = make_promotions<V, Type, UpLeft >(moveList, pop_lsb(&b2), ksq);

        while (b3)
            moveList = make_promotions<V, Type, Up     >(moveList, pop_lsb(&b3), ksq);
    }

    // Standard and en-passant captures
    if (Type == CAPTURES || Type == EVASIONS || Type == NON_EVASIONS)
    {
        Bitboard b1 = shift<UpRight>(pawnsNotOn7) & enemies;
        Bitboard b2 = shift<UpLeft >(pawnsNotOn7) & enemies;

        while (b1)
        {
            Square to = pop_lsb(&b1);
            *moveList++ = make_move(to - UpRight, to);
        }

        while (b2)
        {
            Square to = pop_lsb(&b2);
            *moveList++ = make_move(to - UpLeft, to);
        }

        if (pos.ep_square() != SQ_NONE)
        {
            assert(rank_of(pos.ep_square()) == relative_rank(Us, RANK_6));

            // An en passant capture can be an evasion only if the checking piece
            // is the double pushed pawn and so is in the target. Otherwise this
            // is a discovery check and we are forced to do otherwise.
            if (Type == EVASIONS && !(target & (pos.ep_square() - Up)))
                return moveList;

            b1 = pawnsNotOn7 & pos.attacks_from<PAWN>(pos.ep_square(), Them);

            assert(b1);

            while (b1)
                *moveList++ = make<ENPASSANT>(pop_lsb(&b1), pos.ep_square());
        }
    }

    return moveList;
  }


  template<Variant V, PieceType Pt, bool Checks>
  ExtMove* generate_moves(const Position& pos, ExtMove* moveList, Color us,
                          Bitboard target) {

    static_assert(Pt != KING && Pt != PAWN, "Unsupported piece type in generate_moves()");

    const Square* pl = pos.squares<Pt>(us);

    for (Square from = *pl; from != SQ_NONE; from = *++pl)
    {
        if (Checks)
        {
            if (    (Pt == BISHOP || Pt == ROOK || Pt == QUEEN)
                && !(PseudoAttacks[Pt][from] & target & pos.check_squares(Pt)))
                continue;

            if (pos.blockers_for_king(~us) & from)
                continue;
        }

        Bitboard b = pos.attacks_from<Pt>(from) & target;

        if (Checks)
            b &= pos.check_squares(Pt);

        while (b)
            *moveList++ = make_move(from, pop_lsb(&b));
    }

    return moveList;
  }


  template<Variant V, Color Us, GenType Type>
  ExtMove* generate_all(const Position& pos, ExtMove* moveList, Bitboard target) {

    constexpr CastlingRights OO  = Us & KING_SIDE;
    constexpr CastlingRights OOO = Us & QUEEN_SIDE;
    constexpr bool Checks = Type == QUIET_CHECKS; // Reduce template instantations

#ifdef PLACEMENT
    if (V == CRAZYHOUSE_VARIANT && pos.is_placement() && pos.count_in_hand<ALL_PIECES>(Us)) {} else
    {
#endif
    moveList = generate_pawn_moves<V, Us, Type>(pos, moveList, target);
    moveList = generate_moves<V, KNIGHT, Checks>(pos, moveList, Us, target);
    moveList = generate_moves<V, BISHOP, Checks>(pos, moveList, Us, target);
    moveList = generate_moves<V,   ROOK, Checks>(pos, moveList, Us, target);
    moveList = generate_moves<V,  QUEEN, Checks>(pos, moveList, Us, target);
#ifdef PLACEMENT
    }
#endif
#ifdef CRAZYHOUSE
    if (V == CRAZYHOUSE_VARIANT && Type != CAPTURES && pos.count_in_hand<ALL_PIECES>(Us))
    {
        Bitboard b = Type == EVASIONS ? target ^ pos.checkers() :
                     Type == NON_EVASIONS ? target ^ pos.pieces(~Us) : target;
#ifdef PLACEMENT
        if (pos.is_placement())
            b &= (Us == WHITE ? Rank1BB : Rank8BB);
#endif
        moveList = generate_drops<Us,   PAWN, Checks>(pos, moveList, b & ~(Rank1BB | Rank8BB));
        moveList = generate_drops<Us, KNIGHT, Checks>(pos, moveList, b);
        moveList = generate_drops<Us, BISHOP, Checks>(pos, moveList, b);
        moveList = generate_drops<Us,   ROOK, Checks>(pos, moveList, b);
        moveList = generate_drops<Us,  QUEEN, Checks>(pos, moveList, b);
#ifdef PLACEMENT
        if (pos.is_placement())
            moveList = generate_drops<Us, KING, Checks>(pos, moveList, b);
#endif
    }
#endif

#ifdef HORDE
    if (pos.is_horde() && pos.is_horde_color(Us))
        return moveList;
#endif
    switch (V)
    {
#ifdef ANTI
    case ANTI_VARIANT:
        moveList = generate_king_moves<V, Us, Type>(pos, moveList, target);
        if (pos.can_capture())
            return moveList;
    break;
#endif
#ifdef EXTINCTION
    case EXTINCTION_VARIANT:
        moveList = generate_king_moves<V, Us, Type>(pos, moveList, target);
    break;
#endif
#ifdef TWOKINGS
    case TWOKINGS_VARIANT:
        if (Type != EVASIONS)
            moveList = generate_king_moves<V, Us, Type>(pos, moveList, target);
    break;
#endif
    default:
    if (Type != QUIET_CHECKS && Type != EVASIONS)
    {
        Square ksq = pos.square<KING>(Us);
        Bitboard b = pos.attacks_from<KING>(ksq) & target;
#ifdef RACE
        if (V == RACE_VARIANT)
        {
            // Early generate king advance moves
            if (Type == CAPTURES)
                b |= pos.attacks_from<KING>(ksq) & passed_pawn_span(WHITE, ksq) & ~pos.pieces();
            if (Type == QUIETS)
                b &= ~passed_pawn_span(WHITE, ksq);
        }
#endif
        while (b)
            *moveList++ = make_move(ksq, pop_lsb(&b));
    }
    }
    if (Type != QUIET_CHECKS && Type != EVASIONS)
    {
        Square ksq = pos.square<KING>(Us);
#ifdef GIVEAWAY
        if (V == ANTI_VARIANT && pos.is_giveaway())
            ksq = pos.castling_king_square(Us);
#endif
#ifdef EXTINCTION
        if (V == EXTINCTION_VARIANT)
            ksq = pos.castling_king_square(Us);
#endif
#ifdef TWOKINGS
        if (V == TWOKINGS_VARIANT)
            ksq = pos.castling_king_square(Us);
#endif

#ifdef LOSERS
        if (V == LOSERS_VARIANT && pos.can_capture_losers()) {} else
#endif
        if (Type != CAPTURES && pos.can_castle(CastlingRights(OO | OOO)))
        {
            if (!pos.castling_impeded(OO) && pos.can_castle(OO))
                *moveList++ = make<CASTLING>(ksq, pos.castling_rook_square(OO));

            if (!pos.castling_impeded(OOO) && pos.can_castle(OOO))
                *moveList++ = make<CASTLING>(ksq, pos.castling_rook_square(OOO));
        }
    }

    return moveList;
  }

} // namespace


/// <CAPTURES>     Generates all pseudo-legal captures and queen promotions
/// <QUIETS>       Generates all pseudo-legal non-captures and underpromotions
/// <NON_EVASIONS> Generates all pseudo-legal captures and non-captures
///
/// Returns a pointer to the end of the move list.

template<GenType Type>
ExtMove* generate(const Position& pos, ExtMove* moveList) {

  static_assert(Type == CAPTURES || Type == QUIETS || Type == NON_EVASIONS, "Unsupported type in generate()");
  assert(!pos.checkers());

  Color us = pos.side_to_move();

  Bitboard target =  Type == CAPTURES     ?  pos.pieces(~us)
                   : Type == QUIETS       ? ~pos.pieces()
                   : Type == NON_EVASIONS ? ~pos.pieces(us) : 0;

#ifdef ANTI
  if (pos.is_anti())
  {
      if (pos.can_capture())
          target &= pos.pieces(~us);
      return us == WHITE ? generate_all<ANTI_VARIANT, WHITE, Type>(pos, moveList, target)
                         : generate_all<ANTI_VARIANT, BLACK, Type>(pos, moveList, target);
  }
#endif
#ifdef ATOMIC
  if (pos.is_atomic())
  {
      if (Type == CAPTURES || Type == NON_EVASIONS)
          target &= ~(pos.pieces(~us) & adjacent_squares_bb(pos.pieces(us, KING)));
      return us == WHITE ? generate_all<ATOMIC_VARIANT, WHITE, Type>(pos, moveList, target)
                         : generate_all<ATOMIC_VARIANT, BLACK, Type>(pos, moveList, target);
  }
#endif
#ifdef CRAZYHOUSE
  if (pos.is_house())
      return us == WHITE ? generate_all<CRAZYHOUSE_VARIANT, WHITE, Type>(pos, moveList, target)
                         : generate_all<CRAZYHOUSE_VARIANT, BLACK, Type>(pos, moveList, target);
#endif
#ifdef EXTINCTION
  if (pos.is_extinction())
      return us == WHITE ? generate_all<EXTINCTION_VARIANT, WHITE, Type>(pos, moveList, target)
                         : generate_all<EXTINCTION_VARIANT, BLACK, Type>(pos, moveList, target);
#endif
#ifdef GRID
  if (pos.is_grid())
      return us == WHITE ? generate_all<GRID_VARIANT, WHITE, Type>(pos, moveList, target)
                         : generate_all<GRID_VARIANT, BLACK, Type>(pos, moveList, target);
#endif
#ifdef HORDE
  if (pos.is_horde())
      return us == WHITE ? generate_all<HORDE_VARIANT, WHITE, Type>(pos, moveList, target)
                         : generate_all<HORDE_VARIANT, BLACK, Type>(pos, moveList, target);
#endif
#ifdef LOSERS
  if (pos.is_losers())
  {
      if (pos.can_capture_losers())
          target &= pos.pieces(~us);
      return us == WHITE ? generate_all<LOSERS_VARIANT, WHITE, Type>(pos, moveList, target)
                         : generate_all<LOSERS_VARIANT, BLACK, Type>(pos, moveList, target);
  }
#endif
#ifdef RACE
  if (pos.is_race())
      return us == WHITE ? generate_all<RACE_VARIANT, WHITE, Type>(pos, moveList, target)
                         : generate_all<RACE_VARIANT, BLACK, Type>(pos, moveList, target);
#endif
#ifdef TWOKINGS
  if (pos.is_two_kings())
      return us == WHITE ? generate_all<TWOKINGS_VARIANT, WHITE, Type>(pos, moveList, target)
                         : generate_all<TWOKINGS_VARIANT, BLACK, Type>(pos, moveList, target);
#endif
  return us == WHITE ? generate_all<CHESS_VARIANT, WHITE, Type>(pos, moveList, target)
                     : generate_all<CHESS_VARIANT, BLACK, Type>(pos, moveList, target);
}

// Explicit template instantiations
template ExtMove* generate<CAPTURES>(const Position&, ExtMove*);
template ExtMove* generate<QUIETS>(const Position&, ExtMove*);
template ExtMove* generate<NON_EVASIONS>(const Position&, ExtMove*);


/// generate<QUIET_CHECKS> generates all pseudo-legal non-captures and knight
/// underpromotions that give check. Returns a pointer to the end of the move list.
template<>
ExtMove* generate<QUIET_CHECKS>(const Position& pos, ExtMove* moveList) {
#ifdef ANTI
  if (pos.is_anti())
      return moveList;
#endif
#ifdef EXTINCTION
  if (pos.is_extinction())
      return moveList;
#endif
#ifdef HORDE
  if (pos.is_horde() && pos.is_horde_color(~pos.side_to_move()))
      return moveList;
#endif
#ifdef LOSERS
  if (pos.is_losers() && pos.can_capture_losers())
      return moveList;
#endif
#ifdef PLACEMENT
  if (pos.is_placement() && pos.count_in_hand<KING>(~pos.side_to_move()))
      return moveList;
#endif
#ifdef RACE
  if (pos.is_race())
      return moveList;
#endif

  assert(!pos.checkers());

  Color us = pos.side_to_move();
  Bitboard dc = pos.blockers_for_king(~us) & pos.pieces(us);

  while (dc)
  {
     Square from = pop_lsb(&dc);
     PieceType pt = type_of(pos.piece_on(from));

     if (pt == PAWN)
         continue; // Will be generated together with direct checks

     Bitboard b = pos.attacks_from(pt, from) & ~pos.pieces();

     if (pt == KING)
         b &= ~PseudoAttacks[QUEEN][pos.square<KING>(~us)];

     while (b)
         *moveList++ = make_move(from, pop_lsb(&b));
  }

#ifdef ATOMIC
  if (pos.is_atomic())
      return us == WHITE ? generate_all<ATOMIC_VARIANT, WHITE, QUIET_CHECKS>(pos, moveList, ~pos.pieces())
                         : generate_all<ATOMIC_VARIANT, BLACK, QUIET_CHECKS>(pos, moveList, ~pos.pieces());
#endif
#ifdef CRAZYHOUSE
  if (pos.is_house())
      return us == WHITE ? generate_all<CRAZYHOUSE_VARIANT, WHITE, QUIET_CHECKS>(pos, moveList, ~pos.pieces())
                         : generate_all<CRAZYHOUSE_VARIANT, BLACK, QUIET_CHECKS>(pos, moveList, ~pos.pieces());
#endif
#ifdef GRID
  if (pos.is_grid())
      return us == WHITE ? generate_all<GRID_VARIANT, WHITE, QUIET_CHECKS>(pos, moveList, ~pos.pieces())
                         : generate_all<GRID_VARIANT, BLACK, QUIET_CHECKS>(pos, moveList, ~pos.pieces());
#endif
#ifdef HORDE
  if (pos.is_horde())
      return us == WHITE ? generate_all<HORDE_VARIANT, WHITE, QUIET_CHECKS>(pos, moveList, ~pos.pieces())
                         : generate_all<HORDE_VARIANT, BLACK, QUIET_CHECKS>(pos, moveList, ~pos.pieces());
#endif
#ifdef LOSERS
  if (pos.is_losers())
      return us == WHITE ? generate_all<LOSERS_VARIANT, WHITE, QUIET_CHECKS>(pos, moveList, ~pos.pieces())
                         : generate_all<LOSERS_VARIANT, BLACK, QUIET_CHECKS>(pos, moveList, ~pos.pieces());
#endif
#ifdef TWOKINGS
  if (pos.is_two_kings())
      return us == WHITE ? generate_all<TWOKINGS_VARIANT, WHITE, QUIET_CHECKS>(pos, moveList, ~pos.pieces())
                         : generate_all<TWOKINGS_VARIANT, BLACK, QUIET_CHECKS>(pos, moveList, ~pos.pieces());
#endif
  return us == WHITE ? generate_all<CHESS_VARIANT, WHITE, QUIET_CHECKS>(pos, moveList, ~pos.pieces())
                     : generate_all<CHESS_VARIANT, BLACK, QUIET_CHECKS>(pos, moveList, ~pos.pieces());
}


/// generate<EVASIONS> generates all pseudo-legal check evasions when the side
/// to move is in check. Returns a pointer to the end of the move list.
template<>
ExtMove* generate<EVASIONS>(const Position& pos, ExtMove* moveList) {
#ifdef ANTI
  if (pos.is_anti())
      return moveList;
#endif
#ifdef EXTINCTION
  if (pos.is_extinction())
      return moveList;
#endif
#ifdef PLACEMENT
  if (pos.is_placement() && pos.count_in_hand<KING>(pos.side_to_move()))
      return moveList;
#endif
#ifdef RACE
  if (pos.is_race())
      return moveList;
#endif

  assert(pos.checkers());

  Color us = pos.side_to_move();
  Square ksq = pos.square<KING>(us);
  Bitboard sliderAttacks = 0;
  Bitboard sliders = pos.checkers() & ~pos.pieces(KNIGHT, PAWN);
#ifdef ATOMIC
  Bitboard kingRing = pos.is_atomic() ? adjacent_squares_bb(pos.pieces(~us, KING)) : 0;
#endif

#ifdef ATOMIC
  if (pos.is_atomic())
  {
      // Blasts that explode the opposing king or explode all checkers
      // are counted among evasive moves.
      Bitboard target = pos.pieces(~us) & (pos.checkers() | adjacent_squares_bb(pos.checkers()));
      target |= kingRing;
      target &= pos.pieces(~us) & ~adjacent_squares_bb(pos.pieces(us, KING));
      moveList = (us == WHITE ? generate_all<ATOMIC_VARIANT, WHITE, CAPTURES>(pos, moveList, target)
                              : generate_all<ATOMIC_VARIANT, BLACK, CAPTURES>(pos, moveList, target));
  }
#endif

  // Find all the squares attacked by slider checkers. We will remove them from
  // the king evasions in order to skip known illegal moves, which avoids any
  // useless legality checks later on.
  while (sliders)
  {
      Square checksq = pop_lsb(&sliders);
#ifdef GRID
      if (pos.is_grid())
          sliderAttacks |= (LineBB[checksq][ksq] ^ checksq) & ~pos.grid_bb(checksq);
      else
#endif
      sliderAttacks |= LineBB[checksq][ksq] ^ checksq;
  }

  // Generate evasions for king, capture and non capture moves
  Bitboard b;
#ifdef ATOMIC
  if (pos.is_atomic()) // Generate evasions for king, non capture moves
      b = pos.attacks_from<KING>(ksq) & ~pos.pieces() & ~(sliderAttacks & ~kingRing);
  else
#endif
  b = pos.attacks_from<KING>(ksq) & ~pos.pieces(us) & ~sliderAttacks;
#ifdef LOSERS
  if (pos.is_losers() && pos.can_capture_losers())
      b &= pos.pieces(~us);
#endif
#ifdef TWOKINGS
  // In two kings, legality is checked in in Position::legal
  if (pos.is_two_kings())
  {
      Bitboard kings = pos.pieces(us, KING);
      while (kings)
      {
          Square ksq2 = pop_lsb(&kings);
          Bitboard b2 = pos.attacks_from<KING>(ksq2) & ~pos.pieces(us);
          while (b2)
              *moveList++ = make_move(ksq2, pop_lsb(&b2));
      }
  }
  else
#endif
  while (b)
      *moveList++ = make_move(ksq, pop_lsb(&b));

  if (more_than_one(pos.checkers()))
      return moveList; // Double check, only a king move can save the day

  // Generate blocking evasions or captures of the checking piece
  Square checksq = lsb(pos.checkers());
  Bitboard target;
#ifdef ATOMIC
  if (pos.is_atomic()) // Generate blocking evasions of the checking piece
      target = between_bb(checksq, ksq);
  else
#endif
  target = between_bb(checksq, ksq) | checksq;

#ifdef ATOMIC
  if (pos.is_atomic())
      return us == WHITE ? generate_all<ATOMIC_VARIANT, WHITE, EVASIONS>(pos, moveList, target)
                         : generate_all<ATOMIC_VARIANT, BLACK, EVASIONS>(pos, moveList, target);
#endif
#ifdef CRAZYHOUSE
  if (pos.is_house())
      return us == WHITE ? generate_all<CRAZYHOUSE_VARIANT, WHITE, EVASIONS>(pos, moveList, target)
                         : generate_all<CRAZYHOUSE_VARIANT, BLACK, EVASIONS>(pos, moveList, target);
#endif
#ifdef GRID
  if (pos.is_grid())
      return us == WHITE ? generate_all<GRID_VARIANT, WHITE, EVASIONS>(pos, moveList, target)
                         : generate_all<GRID_VARIANT, BLACK, EVASIONS>(pos, moveList, target);
#endif
#ifdef HORDE
  if (pos.is_horde())
      return us == WHITE ? generate_all<HORDE_VARIANT, WHITE, EVASIONS>(pos, moveList, target)
                         : generate_all<HORDE_VARIANT, BLACK, EVASIONS>(pos, moveList, target);
#endif
#ifdef LOSERS
  if (pos.is_losers())
  {
      if (pos.can_capture_losers())
          target &= pos.pieces(~us);
      return us == WHITE ? generate_all<LOSERS_VARIANT, WHITE, EVASIONS>(pos, moveList, target)
                         : generate_all<LOSERS_VARIANT, BLACK, EVASIONS>(pos, moveList, target);
  }
#endif
#ifdef TWOKINGS
  if (pos.is_two_kings())
      return us == WHITE ? generate_all<TWOKINGS_VARIANT, WHITE, EVASIONS>(pos, moveList, target)
                         : generate_all<TWOKINGS_VARIANT, BLACK, EVASIONS>(pos, moveList, target);
#endif
  return us == WHITE ? generate_all<CHESS_VARIANT, WHITE, EVASIONS>(pos, moveList, target)
                     : generate_all<CHESS_VARIANT, BLACK, EVASIONS>(pos, moveList, target);
}


/// generate<LEGAL> generates all the legal moves in the given position

template<>
ExtMove* generate<LEGAL>(const Position& pos, ExtMove* moveList) {
  // Return immediately at end of variant
  if (pos.is_variant_end())
      return moveList;

  Color us = pos.side_to_move();
  Bitboard pinned = pos.blockers_for_king(us) & pos.pieces(us);
  bool validate = pinned;
#ifdef GRID
  if (pos.is_grid()) validate = true;
#endif
#ifdef RACE
  if (pos.is_race()) validate = true;
#endif
#ifdef TWOKINGS
  if (pos.is_two_kings()) validate = true;
#endif
  Square ksq;
#ifdef HORDE
  if (pos.is_horde() && pos.is_horde_color(pos.side_to_move()))
      ksq = SQ_NONE;
  else
#endif
  ksq = pos.square<KING>(us);
  ExtMove* cur = moveList;
  moveList = pos.checkers() ? generate<EVASIONS    >(pos, moveList)
                            : generate<NON_EVASIONS>(pos, moveList);
  while (cur != moveList)
      if (   (validate || from_sq(*cur) == ksq || type_of(*cur) == ENPASSANT)
#ifdef CRAZYHOUSE
          && !(pos.is_house() && type_of(*cur) == DROP)
#endif
          && !pos.legal(*cur))
          *cur = (--moveList)->move;
#ifdef ATOMIC
      else if (pos.is_atomic() && pos.capture(*cur) && !pos.legal(*cur))
          *cur = (--moveList)->move;
#endif
      else
          ++cur;

  return moveList;
}
