
{-----------------------------------------------------------------------

   Use the recurrence relations from [1] to compute a sorting network
   for a given length n.

   ghci> sort 6
   [[2,3],[1,3],[1,2],[5,6],[4,6],[4,5],[1,4],[2,5],[3,6],[3,5],[2,4],[3,4]]

   This agrees with the example in [1] on page 294. Given a sequence
   of values x[1..n], the symbol [i,j] means replace [x[i],x[j]] by
   [min(x[i],x[j]), max(x[i],x[j])].

   We use p for P in the paper, and p' for P^*.

   [1] R.C.Bose, R.J.Nelson, "A Sorting Problem"

-----------------------------------------------------------------------}

oddlen :: [a] -> Bool
oddlen [] = False
oddlen [a] = True
oddlen (x:xs) = not $ oddlen xs


-- show a network for sorting n items [0..n-1]
sort n = p' [0..n-1]


-- definitions 2.1 and 2.2i through 2.2vi
p :: [a] -> [a] -> [[a]]

p [x] [y]   = [[x,y]]
p [x] [y,z] = [[x,z], [x,y]]
p [x,y] [z] = [[x,z], [y,z]]

p i j

  -- 2.2i
  | (length i == 2*m+1) && (length j == 2*m+1) =
    (p (take m i) (take m j)) ++ (p (drop m i) (drop m j)) ++ (p (drop m i) (take m j))

  -- 2.2ii
  | (length i == 2*m+1) && (length j == 2*m+2) =
    (p (take m i) (take (m+1) j)) ++ (p (drop m i) (drop (m+1) j)) ++ (p (drop m i) (take (m+1) j))

  -- 2.2iii
  | (length i == 2*m+2) && (length j == 2*m+1) =
    (p (take (m+1) i) (take (m+1) j)) ++ (p (drop (m+1) i) (drop (m+1) j)) ++ (p (drop (m+1) i) (take (m+1) j))

  -- 2.2iv
  | (length i == 2*m) && (length j == 2*m) =
    (p (take m i) (take m j)) ++ (p (drop m i) (drop m j)) ++ (p (drop m i) (take m j))

  -- 2.2v
  | (length i == 2*m) && (length j == 2*m+1) =
    (p (take m i) (take (m+1) j)) ++ (p (drop m i) (drop (m+1) j)) ++ (p (drop m i) (take (m+1) j))

  -- 2.2vi
  | (length i == 2*m+1) && (length j == 2*m) =
    (p (take (m+1) i) (take m j)) ++ (p (drop (m+1) i) (drop m j)) ++ (p (drop (m+1) i) (take m j))

  where m = (min (length i) (length j)) `div` 2



{-
z :: [a] -> [a] -> [[a]]
z [x] [y]   = [[x,y]]
z [x] [y,z] = [[x,z], [x,y]]
z [x,y] [z] = [[x,z], [y,z]]
z i j = (z i0 j0) ++ (z i1 j1) ++ (z i1 j0)
  where ((i0,i1),(j0,j1)) = (splitAt mi i, splitAt mj j)
        (mi,mj) = case (length i, length j) of
          (2*m+1, 2*m+1) -> (m,   m  )    -- 2.2i
          (2*m+1, 2*m+2) -> (m,   m+1)    -- 2.2ii
          (2*m+2, 2*m+1) -> (m+1, m+1)    -- 2.2iii
          (2*m,   2*m  ) -> (m,   m  )    -- 2.2iv
          (2*m,   2*m+1) -> (m,   m+1)    -- 2.2v
          (2*m+1, 2*m  ) -> (m+1, m  )    -- 2.2vi
        m = (min (length i) (length j)) `div` 2
-}


-- definitions 5.1 and 5.2i, 5.2ii
p' :: [a] -> [[a]]
p' [] = []
p' [x] = []
p' i = (p' beg) ++ (p' end) ++ (p beg end)
     where (beg,end) = splitAt m i
           m = length i `div` 2
