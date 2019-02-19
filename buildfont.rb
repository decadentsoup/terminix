#!/usr/bin/env ruby
# buildfont.rb - compile unifont sources into bitmap font
# Copyright (C) 2019 Megan Ruggiero. All rights reserved.
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

require 'oily_png'

# Search for and load all desired Unifont glyphs
def load_glyphs
  glyphs = {}

  Dir['unifont/font/plane*/*.hex'].sort.each do |filename|
    # Copyleft contains a glyph in plane01 and space contains one from plane00
    next if filename == 'unifont/font/plane00/copyleft.hex'
    next if filename == 'unifont/font/plane01/space.hex'

    # These should override the values in pua.hex
    override = filename =~ %r{\Aunifont/font/plane00csur/}

    open(filename) do |input|
      input.each do |line|
        code_point, hex = line.chomp.split(?:, 2)
        code_point = code_point.to_i(16)

        if glyphs.include?(code_point) and not override
          puts("Conflicting glyphs at code point #{code_point.to_s(16)}")
        end

        glyphs[code_point] = [hex].pack('H*').unpack('B*')[0]
      end
    end
  end

  glyphs
end

WHITE = ChunkyPNG::Color::WHITE
EMPTY = ChunkyPNG::Color::TRANSPARENT

def generate_page(image, page_index, range, glyphs)
  x, y = 0, page_index * 256

  range.each do |code_point|
    px = x * 16
    py = y * 16
    glyph = glyphs[code_point]

    unless glyph.nil?
      w = glyph.bytesize / 16

      16.times do |ry|
        w.times do |rx|
          if glyph[rx + ry * w] == ?1
            image[px + rx, py + ry] = WHITE
          end
        end
      end
    end

    if x == 255
      x = 0
      y += 1
    else
      x += 1
    end
  end
end

if $0 == __FILE__
  glyphs = load_glyphs

  image = ChunkyPNG::Image.new(16 * 256, 16 * 256 * 3, EMPTY)

  generate_page(image, 0, 0x000000..0x00FFFF, glyphs)
  generate_page(image, 1, 0x010000..0x01FFFF, glyphs)
  generate_page(image, 2, 0x0F0000..0x0FFFFF, glyphs)

  image.save('unifont.png')
end

# vim: set ts=8 sts=2 sw=2 et: