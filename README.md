# Drill
A kakoune roleplayer for yed, might be very rough now, and not have your favourite feature, but thats ok.

### Differences with vim/vimish

* Only 2 modes, Normal and Insert
* Search is also on 's' in normal mode ~~until we get the proper select command, wonder whos responsible for that~~ perhaps wont be needed at all
* Select first, action after
* Delete a character with `d`
* To delete a line do `xd`
* Yank a line `xy`
* I can't think of anything else.
* No regex search

### Diferences with kakoune
* `N` for goint to the previous char, as opposed to `M-a`
* A secret Visual mode, but don't tell kakoune users
  ![20211001_22h01m11s_grim](https://user-images.githubusercontent.com/71751817/135701850-bddce287-dd75-4bf9-8a95-dbc4c876c4a3.png)
* No regex search

Recommended bindings:
```bash
bind "ctrl-o" jump-stack-pop
drill-bind normal "g d" multi jump-stack-push ctags-jump-to-definition
drill-bind normal "g e" 'cursor-buffer-end'
drill-bind normal "g g" 'cursor-buffer-begin'
drill-bind normal "g l" 'cursor-line-end'
drill-bind normal "g h" 'cursor-line-begin'
```
