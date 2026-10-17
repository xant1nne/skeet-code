-- SKEET Code Lua Plugin Example
-- Provides git status tool and custom greeting command

skeet.log(skeet.LOG_INFO, "Loading example_plugin.lua...")

-- Register custom tool
skeet.register_tool({
    name = "lua_quick_notes",
    description = "Saves or reads quick notes in current session via Lua script",
    schema = '{"type":"object","properties":{"action":{"type":"string","description":"read or write"},"note":{"type":"string","description":"note text"}},"required":["action"]}',
    run = function(args)
        if type(args) == "table" and args.action == "write" then
            return "Note saved via Lua plugin: " .. tostring(args.note)
        else
            return "Quick notes from Lua plugin: System operational."
        end
    end
})

-- Register custom slash command
skeet.register_command("/lua_hello", "A test command defined in Lua", function(args)
    print("Hello from Lua script plugin! Argument was: " .. tostring(args))
end)

skeet.log(skeet.LOG_INFO, "example_plugin.lua loaded successfully!")

