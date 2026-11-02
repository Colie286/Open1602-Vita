-- The package's script (scripts.md): every hundredth building a player places is worth a message
-- and 1000 gold.
state = state or {placed = 0}

function on_building_placed(e)
  if not e.player then return end
  state.placed = state.placed + 1
  if state.placed % 100 == 0 then
    game.add_money(e.player, 1000)
    game.message("milestone", {name = game.player(e.player).name, count = state.placed})
  end
end
