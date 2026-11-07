! kernels.f90 KERNEL [PASSES] : FORTRAN-LESSONS-2026-10-15's array kernels in
! Fortran. sum, dot and axpy run over ten million doubles, a(i) = i*1e-7 and
! b(i) = -(i*1e-7) for i from 0; matmul is the naive 200x200 triple loop over
! flat row-major arrays, as kernels.c and kernels.raku write it. The *i kernels
! are the intrinsics a Fortran programmer would write instead (sum,
! dot_product, a whole-array expression, matmul). The arrays are built once and
! the kernel runs PASSES times (5); prints the checksum, then the milliseconds
! of the fastest pass.
program kernels
  implicit none
  integer, parameter :: dp = kind(1.0d0), n = 10000000, m = 200
  real(dp), allocatable :: a(:), b(:), c(:), x(:), y(:), z(:), xr(:,:), yr(:,:), zr(:,:)
  real(dp) :: s, best
  integer :: i, j, k, r, passes
  integer(8) :: t0, t1, rate
  character(len=16) :: which, arg

  call get_command_argument(1, which)
  call get_command_argument(2, arg)
  passes = 5
  if (len_trim(arg) > 0) read(arg, *) passes
  best = huge(1.0d0)
  if (which(1:6) == 'matmul') then
     allocate(x(m*m), y(m*m), z(m*m))
     do i = 1, m*m
        x(i) = mod(i-1, 7) * 0.25d0
        y(i) = mod(i-1, 5) * 0.5d0
     end do
     ! column-major: reshape gives x's transpose, so z's transpose = y' x'
     xr = reshape(x, [m, m])
     yr = reshape(y, [m, m])
     do r = 1, passes
        call system_clock(t0, rate)
        if (trim(which) == 'matmul') then
           do i = 0, m-1
              do j = 0, m-1
                 s = 0
                 do k = 0, m-1
                    s = s + x(i*m + k + 1) * y(k*m + j + 1)
                 end do
                 z(i*m + j + 1) = s
              end do
           end do
        else
           zr = matmul(yr, xr)
        end if
        call system_clock(t1)
        best = min(best, real(t1 - t0, dp) * 1000 / rate)
     end do
     if (trim(which) /= 'matmul') z = reshape(zr, [m*m])
     s = 0
     do i = 1, m*m
        s = s + z(i)
     end do
  else
     allocate(a(n), b(n), c(n))
     do i = 1, n
        a(i) = (i-1) * 1.0d-7
        b(i) = -((i-1) * 1.0d-7)
     end do
     do r = 1, passes
        call system_clock(t0, rate)
        select case (trim(which))
        case ('sum')
           s = 0
           do i = 1, n
              s = s + a(i)
           end do
        case ('sumi')
           s = sum(a)
        case ('dot')
           s = 0
           do i = 1, n
              s = s + a(i) * b(i)
           end do
        case ('doti')
           s = dot_product(a, b)
        case ('axpy')
           do i = 1, n
              c(i) = 2.0d0 * a(i) + b(i)
           end do
        case ('axpyi')
           c = 2.0d0 * a + b
        case default
           stop 'kernel?'
        end select
        call system_clock(t1)
        best = min(best, real(t1 - t0, dp) * 1000 / rate)
     end do
     if (which(1:4) == 'axpy') then
        s = 0
        do i = 1, n
           s = s + c(i)
        end do
     end if
  end if
  print '(es24.16)', s
  print '(f0.3)', best
end program kernels
