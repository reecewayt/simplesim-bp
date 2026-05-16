# Implementation Guide: Tagged Geometric (TAGE) Branch Predictor

This document outlines the architectural and algorithmic details for implementing a **TAGE branch predictor**. TAGE is the state-of-the-art for tagged branch predictors, leveraging multiple tables indexed by geometrically increasing lengths of global history.

---

## 1. Architectural Overview

The predictor consists of a base predictor ($T_0$) and several tagged components ($T_1, T_2, ..., T_n$).

* **$T_0$ (Base Predictor):** A standard 2-bit bimodal predictor that does not use tags.


* **$T_i$ (Tagged Tables):** Components indexed by global history of length $L_i$. Each entry contains:


* **Prediction Counter (ctr):** A 3-bit signed saturating counter with values ranging from -4 to +3.


* **Tag:** A truncated hash used to identify the branch.


* **Useful Bit (u):** A 1-bit or 2-bit counter used specifically for the replacement policy.





## 2. Geometric History Lengths

History lengths follow a geometric series to capture both short-term and very long-term correlations.

* A common approach is calculating $L_i = \text{int}(2^{i-1} \cdot L_1 + 0.5)$.


* For a 4-table setup, example lengths would be $L_1=5, L_2=15, L_3=44$, and $L_4=130$.



## 3. Indexing and Hashing

To minimize collisions and handle variable history lengths, use **Folded History**.

### 3.1 Folded History Registers

For each tagged table, maintain two folded history values:

1. **$idx\_hash$:** Length matches $\log_2(\text{TableSize})$.


2. **$tag\_hash$:** Length matches the Tag width.



**Update Logic for Folded History:**
When a branch shifts a new bit `b` into the Global History Register (GHR):

```
new_folded = (folded << 1) ^ b ^ (GHR[L_i] << (folded_length % L_i))
new_folded &= (1 << folded_length) - 1

```

### 3.2 Component Indexing

* **$T_0$ Index:** `PC % T0_SIZE`

* **$T_i$ Index:** `(PC ^ idx_hash[i]) % Ti_SIZE`

* **$T_i$ Tag:** `(PC ^ tag_hash[i]) % (1 << TagWidth)`


## 4. Prediction Logic

1. **Find Matches:** Search all tagged tables $T_1 \dots T_n$ for a tag match.


2. **Identify Providers:**
* **Provider Component ($P$):** The matching table with the longest history.


* **Alternate Provider ($AltP$):** The matching table with the next longest history. If no other tagged table matches, $AltP$ defaults to the base predictor $T_0$.




3. **Selection:**
* Usually, the prediction is `P.ctr >= 0`.


* **Exception:** If the provider's counter is "weak" (e.g., 0 or -1) and $AltP$ is available, some implementations use $AltP$ to improve accuracy for newly allocated entries.





## 5. Update and Allocation Logic

Updates occur after the branch is resolved with actual outcome $O$.

### 5.1 Provider Update

Update the counter of the component $P$ that provided the prediction.

* If $O == \text{Taken}$, increment $P.ctr$ (saturate at max).


* If $O == \text{Not Taken}$, decrement $P.ctr$ (saturate at min).



### 5.2 Useful Bit (u) Update

Update the `u` bit of the provider $P$ if the $AltP$ prediction differed from the $P$ prediction.

* If $P.pred == O$ and $AltP.pred \neq O$: Increment $u$.


* If $P.pred \neq O$ and $AltP.pred == O$: Decrement $u$.



### 5.3 Allocation on Misprediction

If the overall prediction was wrong and $P$ is not the table with the longest history ($T_n$):

1. Find candidate tables $T_k$ where $k > \text{index of } P$.


2. Look for an entry in $T_k$ where $u == 0$.


3. If multiple $T_k$ have $u == 0$, pick one randomly or the one with the smallest history.


4. If no $T_k$ has $u == 0$, decrement the $u$ bits of all candidate tables $T_k$ and skip allocation for this cycle.


5. **On Allocation:** Initialize $ctr$ to "weak taken" or "weak not-taken" based on outcome $O$, set $u = 0$, and set the $tag$ based on the current PC and history.



## 6. Global History Management

* **GHR:** Use a bitmask or `std::vector<bool>` to represent path history.


* **Path History (Optional):** Some versions XOR the low-order bits of the PC of previous branches into the hashing logic to distinguish different paths leading to the same history.



## 7. Recommended Parameters

* **Tables:** 1 Base + 4 Tagged.


* **Table Sizes:** 512 to 2048 entries per table.


* **Tag Widths:** 8 to 12 bits.


* **Counter:** 3-bit signed (range -4 to +3).


* **Useful bit:** 1-bit or 2-bit.
