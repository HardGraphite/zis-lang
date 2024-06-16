;;; zis-mode.el --- Major mode for editing ZiS source code -*- lexical-binding: t; -*-

;;; Commentary:

;; This is the Emacs major mode for ZiS programming language file editing.

;;; Code:

(eval-when-compile
  (require 'rx))

(defgroup zis nil
  "Major mode for editing ZiS code."
  :prefix "zis-"
  :group 'languages)

(defconst zis-keyword-list
  '( "nil" "true" "false"
     "func" "struct"
     "if" "elif" "else" "while" "for"
     "break" "continue" "import" "return" "throw"
     "end" ))

(defconst zis-font-lock-keywords
  `((,(regexp-opt '("nil" "true" "false") 'symbols) . 'font-lock-builtin-face)
    (,(regexp-opt (cdddr zis-keyword-list) 'symbols) . 'font-lock-keyword-face)
    (,(rx "func" (1+ space) (group (1+ (or word (syntax symbol))))) . 'font-lock-function-name-face)
    (,(rx "struct" (1+ space) (group (1+ (or word (syntax symbol))))) . 'font-lock-type-face)))

(defconst zis-mode-syntax-table
  (let ((table (make-syntax-table)))
    (modify-syntax-entry ?\' "\"'" table)
    (modify-syntax-entry ?\" "\"\"" table)
    (modify-syntax-entry ?\\ "\\" table)
    (modify-syntax-entry ?_ "_" table)
    (modify-syntax-entry ?+ "." table)
    (modify-syntax-entry ?* "." table)
    (modify-syntax-entry ?- "." table)
    (modify-syntax-entry ?/ "." table)
    (modify-syntax-entry ?% "." table)
    (modify-syntax-entry ?< "." table)
    (modify-syntax-entry ?> "." table)
    (modify-syntax-entry ?& "." table)
    (modify-syntax-entry ?| "." table)
    (modify-syntax-entry ?^ "." table)
    (modify-syntax-entry ?= "." table)
    (modify-syntax-entry ?! "." table)
    (modify-syntax-entry ?: "." table)
    (modify-syntax-entry ?@ "." table)
    (modify-syntax-entry ?? "." table)
    (modify-syntax-entry ?$ "." table)
    (modify-syntax-entry ?\( "()" table)
    (modify-syntax-entry ?\) ")(" table)
    (modify-syntax-entry ?\{ "(}" table)
    (modify-syntax-entry ?\} "){" table)
    (modify-syntax-entry ?\[ "(]" table)
    (modify-syntax-entry ?\] ")[" table)
    (modify-syntax-entry ?\; "-" table)
    (modify-syntax-entry ?# "<" table)
    (modify-syntax-entry ?\n ">" table)
    table))

(defvar zis-indent-size 4
  "Indentation size.")

(defun zis-indent-line ()
  "Indent current line for ZiS code."
  (interactive)
  ;; TODO: better indentation support.
  (let ((indent-n 0)
        (prev-line-indent 0)
        (orig-pos (point))
        (orig-col-after-indent nil)
        (first-line-p nil)
        (inhibit-redisplay t))
    (save-excursion
      (beginning-of-line)
      (setq first-line-p (bobp))
      (back-to-indentation) ;; beginning of current line
      (setq orig-col-after-indent (- orig-pos (point)))
      (unless first-line-p
        (when (let ((s (car (syntax-after (point)))))
                (or (= s 5)
                    (and (= s 2)
                         (member (thing-at-point 'symbol t)
                                 '("end" "elif")))))
          (setq indent-n (1- indent-n)))
        (forward-line -1)
        (while (looking-at-p "[[:blank:]]*$")
          (forward-line -1))
        (back-to-indentation) ;; beginning of previous line
        (setq prev-line-indent (- (point) (line-beginning-position)))
        (when (and (= (car (syntax-after (point))) 2)
                   (member (thing-at-point 'symbol t)
                           '("func" "if" "elif" "while" "for")))
          (setq indent-n (1+ indent-n)))
        (end-of-line) ;; end of previous line
        (when (= (car (syntax-after (1- (point)))) 4)
          (setq indent-n (1+ indent-n)))))
    (progn
      (beginning-of-line)
      (delete-horizontal-space)
      (indent-to (max 0 (+ prev-line-indent (* indent-n zis-indent-size)))))
    (when (> orig-col-after-indent 0)
      (forward-char orig-col-after-indent))
    indent-n))

;;;###autoload
(define-derived-mode zis-mode prog-mode "ZiS"
  "Major mode for editing ZiS code."
  (set-syntax-table zis-mode-syntax-table)
  (setq-local font-lock-defaults '(zis-font-lock-keywords))
  (setq-local indent-line-function #'zis-indent-line)
  (setq-local comment-start "# ")
  (setq-local comment-start-skip "#+ *"))

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.zis\\'" . zis-mode))

(provide 'zis-mode)
;;; zis-mode.el ends here
