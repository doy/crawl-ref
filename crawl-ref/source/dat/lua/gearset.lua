------------------------------------------------------------------------
-- gearset.lua:
-- Allows for quick switching between two sets of equipment.
--
-- IMPORTANT
-- This Lua script remembers only the *inventory slots* of the gear you're
-- wearing (it could remember item names, but they're prone to change based on
-- identification, enchantment and curse status). If you swap around items in
-- your inventory, the script may attempt to wear odd items (this will not kill
-- the game, but it can be disconcerting if you're not expecting it).
--
-- To use this, source this file in your init.txt:
--   lua_file = lua/gearset.lua
--
-- Then start Crawl and create two macros:
-- 1. Any macro input key (say F2), set macro action to "===rememberkit"
-- 2. Any macro input key (say F3), set macro action to "===swapkit"
-- It helps to save your macros at this point. :-)
--
-- You can now hit F2 to remember the equipment you're wearing. Once you've
-- defined two sets of equipment, hit F3 to swap between them.
--
-- You can also just define one set of equipment; in this case, every time
-- you hit F3 (swapkit), the macro makes sure you're wearing all the items in
-- that set (and only the items in that set). This is handy for transmuters who 
-- need to kit up after a transform ends.
------------------------------------------------------------------------
function scan_kit()
    local kit = { }
    for i = 9, 0, -1 do
        local it = item.equipped_at(i)
        if it then
            table.insert(kit, item.slot(it))
        end
    end
    return kit
end

function kit_items(kit)
    local items = { }
    local inv = item.inventory()

    for _, slot in ipairs(kit) do
        for _, it in ipairs(inv) do
            if slot == item.slot(it) then
                table.insert(items, it)
            end
        end
    end
    return items
end

function getkey(vkeys)
    local key
    
    while true do
        key = crawl.getch()
        if key == 27 then return "escape" end

        if key > 31 and key < 127 then
            local c = string.lower(string.char(key))
            if string.find(vkeys, c) then
                return c
            end
        end
    end
end

-- Macroable
function rememberkit()
    local kit = scan_kit()
    crawl.mpr("Is this (T)ravel or (B)attle kit? ", "prompt")
    local answer = getkey("tb")
    crawl.mesclr()
    if answer == "escape" then
        return false
    end

    if answer == 't' then
        g_kit_travel = kit
    else
        g_kit_battle = kit
    end
    
    return false
end

function write_array(f, arr, aname)
    file.write(f, aname .. " = { ")
    for i, v in ipairs(arr) do
        file.write(f, v .. ", ")
    end
    file.write(f, "}\n")
end

function gearset_save(file)
    if g_kit_travel then
        write_array(file, g_kit_travel, "g_kit_travel")
    end
    if g_kit_battle then
        write_array(file, g_kit_battle, "g_kit_battle")
    end
end

function matchkit(kit1, kit2)
    local matches = 0
    if not kit2 then
        -- Return a positive match for an empty gearset so that swapkit
        -- switches to the *other* gearset. :-)
        return 500
    end
    for _, v1 in ipairs(kit1) do
        for _, v2 in ipairs(kit2) do
            if v1 == v2 then
                matches = matches + 1
            end
        end
    end
    return matches
end

function wear(it)
    local _, eqt = item.equip_type(it)
    if eqt == "Weapon" then
        return item.wield(it)
    elseif eqt == "Amulet" or eqt == "Ring" then
        return item.puton(it)
    elseif eqt ~= "" then
        return item.wear(it)
    else
        return false
    end
end

function wearkit(kit)
    -- Remove all items not in the kit, then wear all items in the kit
    local currkit = scan_kit()
    local toremove = { }
    local noop = true
    
    for _, v in ipairs(currkit) do
        local found = false
        for _, v1 in ipairs(kit) do
            if v == v1 then
                found = true
                break
            end
        end
        if not found then
            table.insert(toremove, v)
        end
    end

    local remitems = kit_items(toremove)
    for _, it in ipairs(remitems) do
        noop = false
        if not item.remove(it) then
            coroutine.yield(false)
        end
        coroutine.yield(true)
    end

    local kitems = kit_items(kit)

    for _, it in ipairs(kitems) do
        if not item.worn(it) then
            noop = false
            if not wear(it) then
                coroutine.yield(false)
            end
            coroutine.yield(true)
        end
    end

    if noop then
        crawl.mpr("Nothing to do.")
    end
end

-- Macroable
function swapkit()
    if not g_kit_battle and not g_kit_travel then
        crawl.mpr("You need to define a gear set first")
        return false
    end

    local kit = scan_kit()
    if matchkit(kit, g_kit_travel) < matchkit(kit, g_kit_battle) then
        crawl.mpr("Switching to travel kit")
        wearkit(g_kit_travel)
    else
        crawl.mpr("Switching to battle kit")
        wearkit(g_kit_battle)
    end
end

function swapkit_interrupt_macro(interrupt_name)
    return interrupt_name == "hp_loss" or interrupt_name == "stat" or
           interrupt_name == "monster" or interrupt_name == "force"
end

-- Add a macro interrupt hook so that we don't get stopped by any old interrupt
chk_interrupt_macro.swapkit = swapkit_interrupt_macro

-- Add ourselves in the Lua save chain
table.insert(chk_lua_save, gearset_save)
