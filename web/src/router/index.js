import { createRouter, createWebHashHistory } from 'vue-router';
import Main_zh from '../views/Main_zh.vue';
import Main_en from '../views/Main_en.vue';

const routes = [
  {
    path: '/',
    redirect: () => {
      const browserLang = navigator.language.toLowerCase();
      // 用户本地系统语言优先
      if (browserLang.startsWith('zh')) {
        return '/zh';
      } else if (browserLang.startsWith('en')) {
        return '/en';
      }

      // 如果不能识别出 en 或 zh，再回退到备选框的本地记录
      const savedLang = localStorage.getItem('user_lang');
      if (savedLang) return savedLang === 'zh' ? '/zh' : '/en';

      return '/en';
    }
  },
  {
    path: '/zh',
    name: 'HomeZh',
    component: Main_zh
  },
  {
    path: '/en',
    name: 'HomeEn',
    component: Main_en
  }
];

const router = createRouter({
  history: createWebHashHistory(), // recommend using hash history for single-file embedded apps like ESP32
  routes
});

export default router;
