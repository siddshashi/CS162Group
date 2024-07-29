# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(priority-donate-condvar) begin
(priority-donate-condvar) Thread L created.
(priority-donate-condvar) Thread L acquired resource_lock.
(priority-donate-condvar) Thread L acquired cond_lock and sleeps.
(priority-donate-condvar) Thread M created.
(priority-donate-condvar) Thread M acquired cond_lock and sleeps.
(priority-donate-condvar) Thread H created.
(priority-donate-condvar) Main thread calls cond_signal.
(priority-donate-condvar) Thread L releases cond_lock after waking up.
(priority-donate-condvar) Thread L releases resource_lock.
(priority-donate-condvar) Thread H acquired resource_lock.
(priority-donate-condvar) Thread H finished.
(priority-donate-condvar) Thread L finished.
(priority-donate-condvar) Main thread calls cond_signal a second time.
(priority-donate-condvar) Thread M releases cond_lock after waking up.
(priority-donate-condvar) Thread M finished.
(priority-donate-condvar) Main thread finished.
(priority-donate-condvar) end
EOF
pass;
