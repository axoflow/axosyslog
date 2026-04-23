`disk-buffer`: keep the queue alive during reload

Keeping the disk-buffer alive on reload fixes a bug, where a full disk-buffer can
grow infinitely by reloading. It can also cause significant reload speedup.