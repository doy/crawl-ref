local niters = 100

local function test_item_level(place, item, nlevels)
  debug.goto_place(place)
  for i = 1, nlevels do
    crawl.mesclr()
    crawl.mpr(place .. " rune test " .. i .. " of " .. nlevels)
    test.regenerate_level()
    test.map_assert(test.level_contains_item(item),
                    "No " .. item .. " created at " .. place)
  end
end

local function test_item_places(nlevels, level_items)
  for _, place in ipairs(level_items) do
    test_item_level(place[1], place[2], nlevels)
  end
end

test_item_places(niters,
                 { { "Snake:$", "serpentine rune" },
                   { "Shoal:$", "barnacled rune" },
                   { "Swamp:$", "decaying rune" },
                   { "Slime:$", "slimy rune" },
                   { "Vault:$", "silver rune" },
                   { "Coc:$", "icy rune" },
                   { "Tar:$", "bone rune" },
                   { "Dis:$", "iron rune" },
                   { "Geh:$", "obsidian rune" },
                   { "Tomb:$", "golden rune" },
                   { "Zot:$", "Orb of Zot" } })
