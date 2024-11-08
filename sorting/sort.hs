
{-----------------------------------------------------------------------

   Use the recurrence relations from [1] to compute a sorting circuit
   for a given n.

   ghci> sort 6
   [[2,3],[1,3],[1,2],[5,6],[4,6],[4,5],[1,4],[2,5],[3,6],[3,5],[2,4],[3,4]]

   This agrees with the example in [1] on page 294. Given a sequence
   of values x[1..n], the symbol [i,j] means replace [x[i],x[j]] by
   [min(x[i],x[j]), max(x[i],x[j])].

   [1] R.C.Bose, R.J.Nelson, "A Sorting Problem"

-----------------------------------------------------------------------}

-- definitions 2.1 and 2.2i through 2.2vi
p :: [a] -> [a] -> [[a]]
p i j
      | (length i == 1) && (length j == 1) = [i ++ j]
      | (length i == 1) && (length j == 2) = [(i ++ tail j)] ++ [(i ++ [head j])]
      | (length i == 2) && (length j == 1) = [([head i] ++ j)] ++ [(tail i ++ j)]

      -- 2.2i
      | (length i == 2*m+1) && (length j == 2*m+1) =
           (p (take m i) (take m j)) ++
           (p (drop m i) (drop m j)) ++
           (p (drop m i) (take m j))

      -- 2.2ii
      | (length i == 2*m+1) && (length j == 2*m+2) =
           (p (take m i) (take (m+1) j)) ++
           (p (drop m i) (drop (m+1) j)) ++
           (p (drop m i) (take (m+1) j))

      -- 2.2iii
      | (length i == 2*m+2) && (length j == 2*m+1) =
           (p (take (m+1) i) (take (m+1) j)) ++
           (p (drop (m+1) i) (drop (m+1) j)) ++
           (p (drop (m+1) i) (take (m+1) j))

      -- 2.2iv
      | (length i == 2*m) && (length j == 2*m) =
           (p (take m i) (take m j)) ++
           (p (drop m i) (drop m j)) ++
           (p (drop m i) (take m j))

      -- 2.2v
      | (length i == 2*m) && (length j == 2*m+1) =
           (p (take m i) (take (m+1) j)) ++
           (p (drop m i) (drop (m+1) j)) ++
           (p (drop m i) (take (m+1) j))

      -- 2.2vi
      | (length i == 2*m+1) && (length j == 2*m) =
           (p (take (m+1) i) (take m j)) ++
           (p (drop (m+1) i) (drop m j)) ++
           (p (drop (m+1) i) (take m j))

   where m = div (min (length i) (length j)) 2


-- definitions 5.1 and 5.2i, 5.2ii
p' :: [a] -> [[a]]
p' i
      | length i < 2 = []

      -- 5.2i
      | (length i == 2*m) =
           (p' (take m i)) ++
           (p' (drop m i)) ++
           (p (take m i) (drop m i))

      -- 5.2ii
      | (length i == 2*m+1) =
           (p' (take m i)) ++
           (p' (drop m i)) ++
           (p (take m i) (drop m i))

   where m = div (length i) 2


sort n = p' [1..n]

