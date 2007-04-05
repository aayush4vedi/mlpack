#ifndef U_SVM_SMO_H
#define U_SVM_SMO_H

#include "fastlib/fastlib.h"

/* TODO: I don't actually want these to be public */
const double SMO_ZERO = 1.0e-8;
const double SMO_EPS = 1.0e-4;
const double SMO_TOLERANCE = 1.0e-4;

template<typename TKernel>
class SMO {
  FORBID_COPY(SMO);

 public:
  typedef TKernel Kernel;

 private:
  Kernel kernel_;
  const Dataset *dataset_;
  Matrix matrix_;
  Vector alpha_;
  Vector error_;
  double thresh_;
  double c_;

 public:
  SMO() {}
  ~SMO() {}

  /**
   * Initializes an SMO problem.
   *
   * You must initialize separately the kernel.
   */
  void Init(const Dataset* dataset_in, double c_in) {
    c_ = c_in;

    dataset_ = dataset_in;
    matrix_.Alias(dataset_->matrix());

    alpha_.Init(matrix_.n_cols());
    alpha_.SetZero();

    error_.Init(matrix_.n_cols());
    error_.SetZero();

    thresh_ = 0;
  }

  void Train();

  const Kernel& kernel() const {
    return kernel_;
  }

  Kernel& kernel() {
    return kernel_;
  }

  double threshold() const {
    return thresh_;
  }

  void GetSVM(Matrix *support_vectors, Vector *support_alpha) const;

 private:
  index_t TrainIteration_(bool examine_all);

  bool TryChange_(index_t j);

  bool TakeStep_(index_t i, index_t j, double error_j);

  double FixAlpha_(double alpha) const {
    if (alpha < SMO_ZERO) {
      alpha = 0;
    } else if (alpha > c_ - SMO_ZERO) {
      alpha = c_;
    }
    return alpha;
  }

  bool IsBound_(double alpha) const {
    return alpha <= 0 || alpha >= c_;
  }

  double GetLabelSign_(index_t i) const {
    return matrix_.get(matrix_.n_rows()-1, i) * 2.0 - 1.0;
  }

  void GetVector_(index_t i, Vector *v) const {
    matrix_.MakeColumnSubvector(i, 0, matrix_.n_rows()-1, v);
  }

  double Error_(index_t i) const {
    if (!IsBound_(alpha_[i])) {
      return error_[i];
    } else {
      return Evaluate_(i) - GetLabelSign_(i);
    }
  }

  double Evaluate_(index_t i) const;

  double EvalKernel_(index_t i, index_t j) const {
    Vector v_i;
    Vector v_j;

    GetVector_(i, &v_i);
    GetVector_(j, &v_j);

    return kernel_.Eval(v_i, v_j);
  }
};

template<typename TKernel>
void SMO<TKernel>::GetSVM(Matrix *support_vectors, Vector *support_alpha) const {
  index_t n_support = 0;
  index_t i_support = 0;

  for (index_t i = 0; i < alpha_.length(); i++) {
    if (unlikely(alpha_[i] != 0)) {
      n_support++;
    }
  }

  support_vectors->Init(matrix_.n_rows() - 1, n_support);
  support_alpha->Init(n_support);

  for (index_t i = 0; i < alpha_.length(); i++) {
    if (unlikely(alpha_[i] != 0)) {
      Vector source;
      Vector dest;

      GetVector_(i, &source);
      support_vectors->MakeColumnVector(i_support, &dest);
      dest.CopyValues(source);

      (*support_alpha)[i_support] = alpha_[i] * GetLabelSign_(i);
    }
  }
}

template<typename TKernel>
double SMO<TKernel>::Evaluate_(index_t i) const {
  // TODO: This only handles linear
  Vector example;
  GetVector_(i, &example);
  double summation = 0;

  // TODO: This is linear in the size of the training points
  for (index_t j = 0; j < matrix_.n_cols(); j++) {
    if (likely(alpha_[j] != 0)) {
      Vector support_vector;
      GetVector_(j, &support_vector);

      summation +=
          GetLabelSign_(i)
          * alpha_[j]
          * kernel_.Eval(example, support_vector);
    }
  }

  return summation - thresh_;
}

template<typename TKernel>
void SMO<TKernel>::Train() {
  bool examine_all = true;

  do {
    DEBUG_GOT_HERE(0);
    index_t num_changed = TrainIteration_(examine_all);

    if (examine_all) {
      examine_all = false;
    } else if (num_changed == 0) {
      examine_all = true;
    }
  } while (examine_all);
}

template<typename TKernel>
index_t SMO<TKernel>::TrainIteration_(bool examine_all) {
  index_t num_changed = 0;

  for (index_t i = 0; i < alpha_.length(); i++) {
    if ((examine_all || IsBound_(alpha_[i])) && TryChange_(i)) {
      num_changed++;
    }
  }

  return num_changed;
}

template<typename TKernel>
bool SMO<TKernel>::TryChange_(index_t j) {
  double error_j = Error_(j); // WALDO
  double rj = error_j * GetLabelSign_(j);

  if (!((rj < -SMO_TOLERANCE && alpha_[j] < c_)
      || (rj > SMO_TOLERANCE && alpha_[j] > 0))) {
    return false; // nothing changed
  }

  // first try the one we suspect to have the largest yield

  if (error_j > 0) {
    index_t i = -1;
    double error_i = error_j;
    for (index_t k = 0; k < alpha_.length(); k++) {
      if (!IsBound_(alpha_[k]) && error_[k] < error_i) {
        error_i = error_[k];
        i = k;
      }
    }
    if (i != -1 && TakeStep_(i, j, error_j)) {
      return true;
    }
  } else if (likely(error_j < 0)) {
    index_t i = -1;
    double error_i = error_j;
    for (index_t k = 0; k < alpha_.length(); k++) {
      if (!IsBound_(alpha_[k]) && error_[k] > error_i) {
        error_i = error_[k];
        i = k;
      }
    }
    if (i != -1 && TakeStep_(i, j, error_j)) {
      return true;
    }
  }

  DEBUG_GOT_HERE(0);
  // try searching through non-bound examples
  index_t start_i = rand() % alpha_.length();
  index_t i = start_i;

  do {
    if (!IsBound_(alpha_[i]) && TakeStep_(i, j, error_j)) {
      return true;
    }
    i = (i + 1) % alpha_.length();
  } while (i != start_i);

  DEBUG_GOT_HERE(0);
  // try searching through all examples
  start_i = rand() % alpha_.length();
  i = start_i;

  do {
    if (TakeStep_(i, j, error_j)) {
      return true;
    }
    i = (i + 1) % alpha_.length();
  } while (i != start_i);

  return false;
}

template<typename TKernel>
bool SMO<TKernel>::TakeStep_(index_t i, index_t j, double error_j) {
  if (i == j) {
    return false;
  }

  double yi = GetLabelSign_(i);
  double yj = GetLabelSign_(i);
  double alpha_i;
  double alpha_j;
  double thresh_new;
  double l;
  double u;
  double s = yi * yj;
  double error_i = Error_(i);
  double r;// = alpha_[j] * s*alpha_[i] + c_*0.5*(1.0-s);

  if (s < 0) {
    r = alpha_[j] - alpha_[i]; // target values are not equal
  } else {
    r = alpha_[j] + alpha_[i] - c_; // target values are equal
  }

  l = math::ClampNonNegative(r);
  u = c_ + math::ClampNonPositive(r);

  if (l == u) {
    // TODO: might put in some tolerance
    return false;
  }

  // cached kernel values
  double kii = EvalKernel_(i, i);
  double kij = EvalKernel_(i, j);
  double kjj = EvalKernel_(j, j);
  // second derivative of objective function
  double eta = 2 * kij - kii - kjj;


  if (likely(eta < 0)) {
    alpha_j = alpha_[j] - yj * (error_i - error_j) / eta;
    alpha_j = math::ClampRange(alpha_j, l, u);
  } else {
    double fiold = error_i + yi;
    double fjold = error_j + yj;
    double vi = fiold + thresh_ - yi*alpha_[i]*kii - yj*alpha_[j]*kij;
    double vj = fjold + thresh_ - yi*alpha_[i]*kij - yj*alpha_[j]*kjj;
    double fl = alpha_[i] + s*alpha_[j] - s*l;
    double fu = alpha_[i] + s*alpha_[j] - s*u;
    double objlower = fl + l
        - 0.5*kii*fl*fl - 0.5*kjj*l*l
        - s*kij*fl*l - yj*l*vi;
    double objupper = fu + u
        - 0.5*kii*fu*fu - 0.5*kjj*u*u
        - s*kij*fu*u - yj*u*vj;

    if (objlower > objupper + SMO_EPS) {
      alpha_j = l;
    } else if (objlower < objupper - SMO_EPS) {
      alpha_j = u;
    } else {
      alpha_j = alpha_[j];
    }
  }

  alpha_j = FixAlpha_(alpha_j);

  double d_alpha_j = alpha_j - alpha_[j];

  // check if there is progress
  if (fabs(d_alpha_j) < SMO_EPS*(alpha_j + alpha_[j] + SMO_EPS)) {
    return false;
  }

  alpha_i = FixAlpha_(alpha_[i] - s*(d_alpha_j));
  double d_alpha_i = alpha_i - alpha_[i];

  // calculate threshold
  double thresh_i = thresh_ + error_i + yi*d_alpha_i*kii + yj*d_alpha_j*kij;
  double thresh_j = thresh_ + error_j + yi*d_alpha_i*kij + yj*d_alpha_j*kjj;

  if (!IsBound_(alpha_i)) {
    thresh_new = thresh_i;
  } else if (!IsBound_(alpha_j)) {
    thresh_new = thresh_j;
  } else {
    thresh_new = (thresh_i + thresh_j) / 2.0;
  }

  // if not bound, error must be zero
  if (!IsBound_(alpha_i)) {
    error_[i] = 0;
  }
  if (!IsBound_(alpha_j)) {
    error_[j] = 0;
  }
  if (!IsBound_(alpha_i) && !IsBound_(alpha_j)) {
    fprintf(stderr, "Neither ai nor aj are bound.");
  }

  double ti = yi*d_alpha_i;
  double tj = yi*d_alpha_j;
  double d_thresh = thresh_new - thresh_;

  for (index_t k = 0; k < error_.length(); k++) {
    if (likely(k != i)) {
      error_[k] += ti*EvalKernel_(i, k) + tj*EvalKernel_(j, k) - d_thresh;
    }
  }

  thresh_ = thresh_new;
  alpha_[i] = alpha_i;
  alpha_[j] = alpha_j;
  
  return true;
}

#endif
