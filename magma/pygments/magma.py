#! /usr/bin/env python

import argparse
import sys
import pygments.cmdline as _cmdline

def main(args):
  parser = argparse.ArgumentParser()
  parser.add_argument('-l', dest='lexer', type=str)
  opts, rest = parser.parse_known_args(args[1:])
  if str(opts.lexer).lower() == 'magma':
    args = [__file__, '-l', __file__ + ':Magma', '-x', *rest]
  _cmdline.main(args)

if __name__ == '__main__':
  main(sys.argv)

# ----------------------------------------------------------------------
# -- here's the actual MagmaLexer:

from pygments.lexer import RegexLexer, include, words, bygroups, inherit
from pygments.token import Comment, Operator, Keyword, Name, String, Number, \
                           Punctuation, Whitespace, Text

class Magma(RegexLexer):
  name = 'Magma'
  aliases = ['magma']
  filenames = ['*.m', '*.mag']

  # NB: the following categories came from the emacs major-mode package
  keywords =       """ _ by default do is select then to where end until
                       catch elif else when case for function if intrinsic
                       procedure repeat try while """.split()
  operators =      """ adj and cat cmpeq cmpne diff div eq ge gt in join
                       le lt meet mod ne notadj notin notsubset or sdiff
                       subset xor not """.split()
  extra_keywords = """ assert assert2 assert3 break clear continue declare delete
                       error error if eval exit
                       forward fprintf freeze iload import load local print
                       printf quit random read readi require requirege
                       requirerange restore return save time vprint vprintf vtime
                       assigned exists forall """.split()
  constants =      """ true false """.split()
  constructors =   """ car case cop elt ext func hom ideal lideal map
                       ncl pmap proc quo rec recformat rideal sub """.split()
  block_open =     """ try catch case when then else do repeat
                       function procedure intrinsic """.split()
  block_close =    """ until end when elif else catch """.split()

  # hack(?): lump all these together and format them the same
  keywords += operators + extra_keywords + constants + constructors
  keywords += block_open + block_close

  tokens = {

    'root': [

      # first gobble up arbitrary whitespace
      (r'\s+', Whitespace),

      # now comments
      (r'//.*?$', Comment.Singleline),
      (r'/\*', Comment.Multiline, 'comment-multiline'),

      # start with intrinsics, as we have to match the special documentation
      (r'(?s)(intrinsic)(\s+)([^()]+?)(\s*)(\(.*?\))(\s*)(->)?(.*?)?({.*?})',
       bygroups(Keyword, Whitespace, Name.Function, Whitespace, Text,
                Whitespace, Operator, Text, Comment)),

      # try and match function/procedure names
      (r'(function|procedure|intrinsic)(\s+)(\w+)',
       bygroups(Keyword, Whitespace, Name.Function)),

      # reserved/keywords
      (words((keywords),prefix=r'\b', suffix=r'\b'), Keyword.Reserved),

      # built in functions are camel-case
      (r'([A-Z][a-z]+)+', Name.Builtin),

      # other identifiers
      (r'\w+', Name),

      # strings, handle embedded quoted quotes
      (r'"', String, 'string'),

      include('numbers'),

      # &
      (r'&(\w+|[\W\S])', Operator),

      # now some special operators (TODO: add more?)
      (r'(:=|->)', Operator),

      # and finally (?) arbitrary symbols
      (r'[^\w\s]', Punctuation),
    ],

    'string': [
      (r'[^\\"]+', String),   # skip over safe string symbols
      (r'\\"', String),       # skip over escaped quotes
      (r'"', String, '#pop'), # unescaped quotes finish the string
      (r'.', String),         # still in string... skip a character
    ],

    'comment-multiline': [
      (r'[^*]+', Comment.Multiline),        # gobble up things until maybe done
      (r'\*+/', Comment.Multiline, '#pop'), # are we done? (gobble up leading stars)
      (r'\*+', Comment.Multiline)           # skip over false alarms
    ],

    'numbers': [
      # TODO: check I'm happy with these...
      (r'0[xX][a-fA-F0-9]+', Number.Hex),
      (r'0[bB][01]+', Number.Bin),
      (r'([0-9]+\.[0-9]*)|([0-9]*\.[0-9]+)', Number.Float),
      (r'[0-9]+', Number.Integer),
    ],
  }

# -*- mode: Python; python-indent-offset: 2; python-guess-indent: nil -*-
