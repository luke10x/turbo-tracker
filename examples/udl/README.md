# UDL fractions and swing

At 80 BPM, this song plays two matching 16-row patterns, each with eight short sample hits on every other row. The first pattern is straight. The second delays its fifth hit on row `08` with `UDL80`, making that hit land about halfway through its row. Compare the two patterns as they play in order to hear the swing against the straight reference.

The final pattern compares table timing in stereo. `TBL00` plays the hits immediately on the left; `TBL01` plays them on the right and delays its fifth hit at table tick `08` with `UDL80`. In a table, the fraction is measured against one table tick's duration.

UDL is the value divided by `FF`, multiplied by the current row duration. Common fractions are `UDL80` ≈ 1/2 (`80/FF` = 128/255) and `UDL55` = 1/3 (`55/FF` = 85/255). `UDLFF` delays to the end of the row. In tables, it delays by the same fraction of one table tick.
