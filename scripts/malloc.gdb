# shows a dot for every malloc
set breakpoint pending on
set $enable_malloc_debug = 0
break malloc
commands
silent
if $enable_malloc_debug >= 1
    printf ".\n"
end
if $enable_malloc_debug >= 2
    bt
end
continue
end
printf "Enable malloc debug with            'set $enable_malloc_debug = 1'\n"
printf "Enable malloc debug backtraces with 'set $enable_malloc_debug = 2'\n"
